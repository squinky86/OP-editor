// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "MainWindow.h"

#include "Dialogs.h"
#include "LyricsPanel.h"
#include "Panels.h"
#include "ScoreView.h"
#include "SongBrowser.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace ope::ui {
namespace {

QString settingsKeyRoot() { return QStringLiteral("songsRoot"); }

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_session(this), m_audio(this)
{
    QSettings settings;
    m_library.setRoot(settings.value(settingsKeyRoot()).toString());
    if (!m_library.root().isEmpty())
        m_library.rescan();

    buildLayout();
    buildMenus();
    updateWindowTitle();

    if (!m_audio.isAvailable())
        m_transport->setUnavailable(m_audio.unavailableReason());

    connect(&m_session, &Session::documentChanged, this, [this] {
        m_planStale = true;
        updateWindowTitle();
        m_statusSummary->setText(m_problems->summary());
    });
    connect(&m_session, &Session::dirtyChanged, this, &MainWindow::updateWindowTitle);
    connect(&m_session, &Session::languageChanged, this, &MainWindow::updateLanguageTabs);

    connect(&m_audio, &audio::AudioEngine::positionChanged, this, [this](double seconds) {
        m_transport->setPosition(seconds, m_audio.duration());
        m_score->setPlaybackTick(m_audio.plan().tickAt(seconds));
    });
    connect(&m_audio, &audio::AudioEngine::playingChanged, this, [this](bool playing) {
        m_transport->setPlaying(playing);
        if (!playing)
            m_score->clearPlaybackTick();
    });
    connect(&m_audio, &audio::AudioEngine::finished, this, [this] {
        m_transport->setPosition(0, m_audio.duration());
        m_score->clearPlaybackTick();
    });

    resize(1500, 940);
    // The score is the point of the window; the two side docks get what they
    // need to be readable and no more.
    resizeDocks({ m_browserDock, m_songDock }, { 270, 330 }, Qt::Horizontal);
}

void MainWindow::buildLayout()
{
    m_score = new ScoreView(&m_session, this);
    m_lyrics = new LyricsPanel(&m_session, this);
    m_source = new SourcePanel(&m_session, this);

    m_centre = new QTabWidget(this);
    m_centre->addTab(m_score, tr("Score"));
    m_centre->addTab(m_lyrics, tr("Lyrics"));
    m_centre->addTab(m_source, tr("Source"));

    m_languageTabs = new QTabWidget(this);
    m_languageTabs->setDocumentMode(true);
    m_languageTabs->setTabPosition(QTabWidget::North);
    m_languageTabs->hide();

    m_transport = new TransportBar(&m_session, this);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_languageTabs);
    layout->addWidget(m_centre, 1);
    layout->addWidget(m_transport);
    setCentralWidget(central);

    // The songs folder is the left-hand column of the window, not a dialog: the
    // job is "work through a directory of hymns", so the directory stays put.
    m_browser = new SongBrowser(&m_library, this);
    m_browserDock = new QDockWidget(tr("Songs"), this);
    m_browserDock->setObjectName(QStringLiteral("songsDock"));
    m_browserDock->setWidget(m_browser);
    addDockWidget(Qt::LeftDockWidgetArea, m_browserDock);
    connect(m_browser, &SongBrowser::openRequested, this, &MainWindow::openPath);
    connect(m_browser, &SongBrowser::newSongRequested, this, &MainWindow::newSong);
    connect(m_browser, &SongBrowser::changeFolderRequested, this, &MainWindow::editPreferences);

    m_header = new HeaderPanel(&m_session, this);
    m_songDock = new QDockWidget(tr("Song"), this);
    m_songDock->setObjectName(QStringLiteral("songDock"));
    m_songDock->setWidget(m_header);
    addDockWidget(Qt::RightDockWidgetArea, m_songDock);

    m_inspector = new InspectorPanel(&m_session, this);
    auto *inspectorDock = new QDockWidget(tr("Inspector"), this);
    inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    inspectorDock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    tabifyDockWidget(m_songDock, inspectorDock);
    m_songDock->raise();

    m_problems = new ProblemsPanel(&m_session, this);
    auto *problemsDock = new QDockWidget(tr("Problems"), this);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(m_problems);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);

    m_statusSummary = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusSummary);

    connect(m_problems, &ProblemsPanel::navigate, this, &MainWindow::navigateTo);
    connect(m_score, &ScoreView::statusMessage, this,
        [this](const QString &message) { statusBar()->showMessage(message, 4000); });
    connect(m_lyrics, &LyricsPanel::statusMessage, this,
        [this](const QString &message) { statusBar()->showMessage(message, 4000); });
    connect(m_score, &ScoreView::requestLyricEdit, this, [this](const QString &part, int slot) {
        m_centre->setCurrentWidget(m_lyrics);
        m_lyrics->focusSlot(part, slot);
    });

    connect(m_transport, &TransportBar::playRequested, this, [this] {
        refreshPlaybackPlan();
        m_audio.play(0.0);
    });
    connect(m_transport, &TransportBar::pauseRequested, this, [this] { m_audio.pause(); });
    connect(m_transport, &TransportBar::stopRequested, this, [this] {
        m_audio.stop();
        m_score->clearPlaybackTick();
    });
    connect(m_transport, &TransportBar::optionsChanged, this, [this] {
        m_planStale = true;
        updateTransportDuration();
    });
    connect(m_transport, &TransportBar::verseChanged, this,
        [this](int verse) { m_score->setActiveVerse(verse); });

    connect(m_languageTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index < 0)
            return;
        const QString code = m_languageTabs->tabBar()->tabData(index).toString();
        if (!code.isEmpty())
            m_session.setCurrentLanguage(code);
    });
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Find Song…"), QKeySequence::Open, this, &MainWindow::showBrowser);
    fileMenu->addAction(tr("Re&fresh Song List"), QKeySequence(Qt::Key_F5), this,
        [this] { m_browser->refresh(); });
    fileMenu->addAction(tr("&New Song…"), QKeySequence::New, this, &MainWindow::newSong);
    fileMenu->addAction(tr("Add &Translation…"), this, &MainWindow::addTranslation);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    fileMenu->addAction(tr("&Reload from Disk"), this, &MainWindow::reloadFromDisk);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Preferences…"), this, &MainWindow::editPreferences);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = m_session.undoStack()->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction *redo = m_session.undoStack()->createRedoAction(this, tr("&Redo"));
    redo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undo);
    editMenu->addAction(redo);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *phrased = viewMenu->addAction(tr("&Phrased line breaks"));
    phrased->setCheckable(true);
    phrased->setChecked(m_score->phrasedLayout());
    connect(phrased, &QAction::toggled, this,
        [this](bool on) { m_score->setPhrasedLayout(on); });
    auto *allVerses = viewMenu->addAction(tr("Show &all verses under the staff"));
    allVerses->setCheckable(true);
    allVerses->setChecked(m_score->showAllVerses());
    allVerses->setToolTip(tr("Off: only the transport's verse, plus the chorus and coda"));
    connect(allVerses, &QAction::toggled, this,
        [this](bool on) { m_score->setShowAllVerses(on); });
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Songs &list"), QKeySequence(Qt::CTRL | Qt::Key_L), this,
        &MainWindow::showBrowser);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Score"), QKeySequence(Qt::CTRL | Qt::Key_1), this,
        [this] { m_centre->setCurrentWidget(m_score); });
    viewMenu->addAction(tr("&Lyrics"), QKeySequence(Qt::CTRL | Qt::Key_2), this,
        [this] { m_centre->setCurrentWidget(m_lyrics); });
    viewMenu->addAction(tr("So&urce"), QKeySequence(Qt::CTRL | Qt::Key_3), this,
        [this] { m_centre->setCurrentWidget(m_source); });

    auto *playMenu = menuBar()->addMenu(tr("&Play"));
    playMenu->addAction(tr("&Play / Pause"), QKeySequence(Qt::Key_Space), this, [this] {
        if (m_audio.isPlaying()) {
            m_audio.pause();
        } else {
            refreshPlaybackPlan();
            m_audio.play(0.0);
        }
    });
    playMenu->addAction(tr("&Stop"), this, [this] { m_audio.stop(); });

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&Keyboard Reference"), this, [this] {
        QMessageBox::information(this, tr("Keyboard Reference"),
            tr("<b>In the score</b><br>"
               "← →  previous / next note<br>"
               "↑ ↓  pitch up / down one step<br>"
               "Shift+↑↓  by a semitone &nbsp; Ctrl+↑↓  by an octave<br>"
               "Alt+↑↓  move to the part above / below<br>"
               "1 2 4 8 6 3  whole, half, quarter, eighth, 16th, 32nd<br>"
               ".  add a dot &nbsp; R  rest &nbsp; P  spacer &nbsp; N  note<br>"
               "T  tie &nbsp; S / Shift+S  slur start / end<br>"
               "B / Shift+B  beam start / end &nbsp; D / Shift+D  dashed slur<br>"
               "F  fermata &nbsp; K  staccato &nbsp; C  @c &nbsp; E  @e<br>"
               "Ins / Del  insert / delete a note<br>"
               "Ctrl+B  phrase break here &nbsp; Ctrl+Shift+B  optional &nbsp; "
               "Alt+B  non-breaking<br>"
               "Ctrl+Enter  copy this marking to every sounding voice<br>"
               "Ctrl+wheel  zoom"
               "<br><br><b>Anywhere</b><br>"
               "Space  play / pause &nbsp; F5  re-read the songs folder<br>"
               "Ctrl+O or Ctrl+L  jump to the song list<br>"
               "Ctrl+1 / Ctrl+2 / Ctrl+3  score / lyrics / source"));
    });
    helpMenu->addAction(tr("&About"), this, [this] { showAbout(this); });
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("OpenPsalm Editor");
    if (m_session.isOpen()) {
        const SongDocument &doc = m_session.document();
        const QString name = QFileInfo(doc.path).fileName();
        title = QStringLiteral("%1 — %2 [%3]%4")
                    .arg(doc.title.valueOr(tr("(untitled)")), name, m_session.currentLanguage(),
                        m_session.isDirty() ? QStringLiteral(" •") : QString());
    }
    setWindowTitle(title);
}

void MainWindow::updateLanguageTabs()
{
    m_languageTabs->blockSignals(true);
    while (m_languageTabs->count() > 0)
        m_languageTabs->removeTab(0);
    const QStringList languages = m_session.languages();
    for (const QString &code : languages) {
        const LanguageInfo *info = i18n::lookup(code);
        const QString label = info ? QStringLiteral("%1 (%2)").arg(info->nativeName, code) : code;
        const int index = m_languageTabs->addTab(new QWidget(m_languageTabs), label);
        m_languageTabs->tabBar()->setTabData(index, code);
        if (code == m_session.currentLanguage())
            m_languageTabs->setCurrentIndex(index);
    }
    m_languageTabs->setVisible(languages.size() > 1);
    m_languageTabs->blockSignals(false);
}

void MainWindow::refreshPlaybackPlan()
{
    if (!m_planStale)
        return;
    m_audio.setPlan(m_session.buildPlaybackPlan(m_transport->options()));
    m_planStale = false;
    m_transport->setDuration(m_audio.duration());
}

void MainWindow::updateTransportDuration()
{
    // How long the song runs is the one thing the transport can say before
    // anyone presses Play, and "0:00 / 0:00" beside a working Play button reads
    // like a broken transport.
    m_transport->setDuration(m_session.isOpen()
            ? m_session.buildPlaybackPlan(m_transport->options()).totalSeconds
            : 0.0);
}

void MainWindow::openPath(const QString &path)
{
    // A click in the songs list has already moved the highlight, so an abandoned
    // open has to put it back on the song that is actually loaded.
    const QString current = m_session.isOpen()
        ? (m_session.baseDocument() ? m_session.baseDocument()->path : m_session.currentPath())
        : QString();
    if (!confirmDiscard()) {
        m_browser->showCurrent(current);
        return;
    }
    if (auto opened = m_session.openSong(path); !opened) {
        showParseError(this, opened.error());
        m_browser->showCurrent(current);
        return;
    }
    m_planStale = true;
    updateLanguageTabs();
    updateWindowTitle();
    m_browser->showCurrent(path);
    updateTransportDuration();
    statusBar()->showMessage(tr("Opened %1").arg(path), 4000);
}

void MainWindow::selectTab(int index)
{
    if (index >= 0 && index < m_centre->count())
        m_centre->setCurrentIndex(index);
}

void MainWindow::showBrowser()
{
    if (m_library.root().isEmpty()) {
        editPreferences();
        if (m_library.root().isEmpty())
            return;
    }
    m_browserDock->show();
    m_browserDock->raise();
    m_browser->focusSearch();
}

void MainWindow::newSong()
{
    if (m_library.root().isEmpty()) {
        editPreferences();
        if (m_library.root().isEmpty())
            return;
    }
    if (!confirmDiscard())
        return;

    m_library.rescan();
    NewSongDialog dialog(&m_library, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString path = dialog.targetPath();
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("New Song"),
            tr("%1 already exists. Pick a different song number.").arg(path));
        return;
    }
    QDir().mkpath(QFileInfo(path).path());
    m_session.adoptNewDocument(dialog.buildDocument());
    updateLanguageTabs();
    updateTransportDuration();
    statusBar()->showMessage(tr("New song ready — nothing is written until you save."), 6000);
}

void MainWindow::addTranslation()
{
    if (!m_session.isOpen()) {
        QMessageBox::information(this, tr("Add Translation"), tr("Open a song first."));
        return;
    }
    const SongDocument *base = m_session.baseDocument();
    if (!base) {
        QMessageBox::information(this, tr("Add Translation"),
            tr("Translations are added to a base song.toml."));
        return;
    }
    if (m_session.isDirty()) {
        QMessageBox::information(this, tr("Add Translation"),
            tr("Save the current changes first — a new translation is written beside the "
               "song on disk."));
        return;
    }

    TranslationDialog dialog(*base, m_session.languages(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString code = dialog.languageCode();
    if (code.isEmpty())
        return;

    SongDocument overlay = dialog.buildOverlay();
    const QByteArray bytes = io::serializeFresh(overlay);
    if (auto written = io::writeAtomically(overlay.path, bytes); !written) {
        QMessageBox::critical(this, tr("Add Translation"),
            tr("Could not write %1: %2").arg(overlay.path, written.error()));
        return;
    }
    openPath(base->path);
    m_session.setCurrentLanguage(code);
    updateLanguageTabs();
    statusBar()->showMessage(tr("Created %1").arg(overlay.path), 6000);
}

void MainWindow::save()
{
    if (!m_session.isOpen())
        return;

    // Land everything the user has typed but not yet left. The header's
    // copyright and commentary boxes commit on focus-out and the lyric boxes on
    // a 600 ms debounce; Ctrl+S is a shortcut, so it moves no focus and beats
    // the timer. Without this the edit is not in the document when it is
    // written, and the refresh that follows the save overwrites the box with
    // the value that was saved — the text vanishes in front of the user.
    m_header->commitPendingEdits();
    m_lyrics->commitPendingEdits();

    const int errors = countBySeverity(m_session.findings(), Severity::Error);
    if (errors > 0) {
        QStringList firstFew;
        for (const Finding &finding : m_session.findings()) {
            if (finding.severity != Severity::Error)
                continue;
            firstFew.append(QStringLiteral("• %1").arg(finding.formatted()));
            if (firstFew.size() == 2)
                break;
        }
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Save with errors?"));
        box.setText(tr("%n error(s) would stop this song seeding, or make it seed wrongly.",
            nullptr, errors));
        box.setInformativeText(firstFew.join(u'\n'));
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.button(QMessageBox::Save)->setText(tr("Save anyway"));
        if (box.exec() != QMessageBox::Save)
            return;
    }

    if (auto saved = m_session.save(); !saved) {
        QMessageBox::critical(this, tr("Save failed"), saved.error());
        return;
    }
    statusBar()->showMessage(tr("Saved %1").arg(m_session.currentPath()), 4000);
    updateWindowTitle();
    // A new song only exists in the list once it has been written.
    m_browser->refresh();
    m_browser->showCurrent(m_session.baseDocument() ? m_session.baseDocument()->path
                                                    : m_session.currentPath());
}

void MainWindow::reloadFromDisk()
{
    if (!m_session.isOpen())
        return;
    if (!confirmDiscard())
        return;
    const SongDocument *base = m_session.baseDocument();
    const QString path = base ? base->path : m_session.currentPath();
    openPath(path);
}

void MainWindow::editPreferences()
{
    PreferencesDialog dialog(m_library.root(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString root = dialog.songsRoot();
    if (!root.isEmpty() && !Library::looksLikeSongsRoot(root)) {
        QMessageBox::warning(this, tr("Preferences"),
            tr("%1 has no numbered song directories in it. Point this at the OP-songs "
               "checkout — the folder containing 1/, 2/, 3/ …")
                .arg(root));
    }
    m_library.setRoot(root);
    QSettings().setValue(settingsKeyRoot(), root);
    m_browser->refresh();
}

bool MainWindow::confirmDiscard()
{
    if (!m_session.isDirty())
        return true;
    const QMessageBox::StandardButton answer = QMessageBox::question(this,
        tr("Unsaved changes"),
        tr("%1 has unsaved changes. Save before continuing?")
            .arg(QFileInfo(m_session.currentPath()).fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        save();
        return !m_session.isDirty();
    }
    return true;
}

void MainWindow::navigateTo(const Finding &finding)
{
    const SongDocument &doc = m_session.effectiveDocument();
    if (!finding.lyricKey.isEmpty() && finding.measure < 0) {
        m_centre->setCurrentWidget(m_lyrics);
        m_lyrics->focusSlot(finding.partName, std::max(0, finding.slot));
        return;
    }
    if (finding.measure > 0) {
        int partIndex = 0;
        for (int i = 0; i < doc.parts.size(); ++i) {
            if (doc.parts.at(i).name == finding.partName)
                partIndex = i;
        }
        m_centre->setCurrentWidget(m_score);
        m_session.setSelection(Selection { partIndex, finding.measure - 1,
            std::max(0, finding.eventIndex) });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (confirmDiscard())
        event->accept();
    else
        event->ignore();
}

} // namespace ope::ui
