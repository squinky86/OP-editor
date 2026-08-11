// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "MainWindow.h"

#include "cli/Contribution.h"
#include "CorpusDownloadDialog.h"
#include "Dialogs.h"
#include "LyricsPanel.h"
#include "Panels.h"
#include "ScoreView.h"
#include "SongBrowser.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QUrlQuery>

namespace ope::ui {
namespace {

QString settingsKeyRoot() { return QStringLiteral("songsRoot"); }

QString internalCorpusRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("OP-songs"));
}

bool isInsideDirectory(const QString &path, const QString &directory)
{
    if (path.isEmpty() || directory.isEmpty())
        return false;
    const QString relative = QDir(QFileInfo(directory).absoluteFilePath())
                                 .relativeFilePath(QFileInfo(path).absoluteFilePath());
    return relative == QLatin1String(".")
        || (relative != QLatin1String("..")
            && !relative.startsWith(QLatin1String("../")) && !QDir::isAbsolutePath(relative));
}

QString tomlCodeBlock(const QByteArray &toml)
{
    QByteArray fence("```");
    while (toml.contains(fence))
        fence += '`';
    QByteArray block = "## Proposed `song.toml`\n\n" + fence + "toml\n" + toml;
    if (!block.endsWith('\n'))
        block += '\n';
    block += fence + '\n';
    return QString::fromUtf8(block);
}

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
    connect(&m_session, &Session::dirtyChanged, this, &MainWindow::updateLanguageTabs);
    connect(&m_session, &Session::languageChanged, this, &MainWindow::updateLanguageTabs);
    const auto pendingChanged = [this](bool) {
        updateWindowTitle();
        updateLanguageTabs();
    };
    connect(m_header, &HeaderPanel::pendingEditsChanged, this, pendingChanged);
    connect(m_lyrics, &LyricsPanel::pendingEditsChanged, this, pendingChanged);

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
    resizeDocks({ m_problemsDock }, { 170 }, Qt::Vertical);
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
    m_problemsDock = new QDockWidget(tr("Problems"), this);
    m_problemsDock->setObjectName(QStringLiteral("problemsDock"));
    m_problemsDock->setWidget(m_problems);
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);

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
        m_audio.resume();
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
        if (!code.isEmpty()) {
            // The widgets still belong to the old language at this point. Land
            // their drafts there before changing the session's identity.
            flushPendingEdits();
            m_session.setCurrentLanguage(code);
        }
    });
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Find Song…"), QKeySequence::Open, this, &MainWindow::showBrowser);
    fileMenu->addAction(tr("Re&fresh Song List"), QKeySequence(Qt::Key_F5), this,
        [this] { m_browser->refresh(); });
    fileMenu->addAction(tr("Download &Latest OP-songs…"), this,
        &MainWindow::downloadLatestCorpus);
    fileMenu->addAction(tr("&New Song…"), QKeySequence::New, this, &MainWindow::newSong);
    fileMenu->addAction(tr("Add &Translation…"), this, &MainWindow::addTranslation);
    fileMenu->addAction(tr("Prepare &Contribution…"), this,
        &MainWindow::prepareContribution);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Save Current"), QKeySequence::Save, this, &MainWindow::save);
    fileMenu->addAction(tr("Save &All"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this,
        [this] { (void)saveAll(); });
    fileMenu->addAction(tr("&Reload from Disk"), this, &MainWindow::reloadFromDisk);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Preferences…"), this, &MainWindow::editPreferences);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = new QAction(tr("&Undo"), this);
    undo->setShortcut(QKeySequence::Undo);
    undo->setEnabled(false);
    connect(undo, &QAction::triggered, this, [this] {
        flushPendingEdits();
        m_session.undoGroup()->undo();
    });
    connect(m_session.undoGroup(), &QUndoGroup::canUndoChanged, undo, &QAction::setEnabled);
    connect(m_session.undoGroup(), &QUndoGroup::undoTextChanged, undo,
        [this, undo](const QString &text) {
            undo->setText(text.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(text));
        });
    QAction *redo = new QAction(tr("&Redo"), this);
    redo->setShortcut(QKeySequence::Redo);
    redo->setEnabled(false);
    connect(redo, &QAction::triggered, this, [this] {
        flushPendingEdits();
        m_session.undoGroup()->redo();
    });
    connect(m_session.undoGroup(), &QUndoGroup::canRedoChanged, redo, &QAction::setEnabled);
    connect(m_session.undoGroup(), &QUndoGroup::redoTextChanged, redo,
        [this, redo](const QString &text) {
            redo->setText(text.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(text));
        });
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
            m_audio.resume();
        }
    });
    playMenu->addAction(tr("&Stop"), this, [this] { m_audio.stop(); });

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&TOML Field Reference"), this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(tr("TOML Field Reference"));
        box.setIcon(QMessageBox::Information);
        box.setText(tr("Every field used by the current OpenPsalm corpus has an editing "
                       "surface in OPE."));
        box.setInformativeText(tr(
            "<b>Song dock</b><br>title, subtitle, active, copyrights, key_signature, "
            "time_sig_numerator, time_sig_denominator, tempo_bpm, verse_count, "
            "converge_verses, commentary, and [[time_sig_changes]].<br><br>"
            "<b>Score and Lyrics tabs</b><br>notes, text, phrase_breaks, "
            "optional_phrase_breaks, and non_breaking_phrase_breaks.<br><br>"
            "<b>Inspector dock</b><br>choral_type, clef, staff_number, "
            "splice_lyrics_into, suppress_verses, and suppress_verses_when.<br><br>"
            "<b>Translations</b><br>File → Add Translation creates song_LANG.toml safely. "
            "The language is its filename suffix.<br><br>"
            "For a future or advanced field OPE does not yet model, use Source → Open "
            "file in text editor. Unknown TOML is preserved byte-for-byte, and OPE "
            "detects external changes before saving."));
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    });
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
    helpMenu->addAction(tr("&Report a Song Problem…"), this,
        &MainWindow::reportSongProblem);
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
                        hasUnsavedWork() ? QStringLiteral(" •") : QString());
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
        QString label = info ? QStringLiteral("%1 (%2)").arg(info->nativeName, code) : code;
        const bool pending = code == m_session.currentLanguage()
            && ((m_header && m_header->hasPendingEdits())
                || (m_lyrics && m_lyrics->hasPendingEdits()));
        if (m_session.isDirty(code) || pending)
            label.append(QStringLiteral(" •"));
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
    m_browser->showCurrent(m_session.baseDocument() ? m_session.baseDocument()->path
                                                    : m_session.currentPath());
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
    const QFileInfo target(path);
    const QDir targetDirectory(target.path());
    if (target.exists()
        || (targetDirectory.exists()
            && !targetDirectory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())) {
        QMessageBox::warning(this, tr("New Song"),
            tr("The automatically selected local draft folder %1 became occupied while the "
               "dialog was open. Try New Song again; OPE will select another local slot.")
                .arg(target.path()));
        return;
    }
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
    TranslationDialog dialog(*base, m_session.languages(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString code = dialog.languageCode();
    if (code.isEmpty())
        return;

    SongDocument overlay = dialog.buildOverlay();
    if (!m_session.adoptNewOverlay(std::move(overlay))) {
        QMessageBox::critical(this, tr("Add Translation"),
            tr("That language is already open. No file was written."));
        return;
    }
    updateLanguageTabs();
    statusBar()->showMessage(
        tr("Translation ready — nothing is written until you save."), 6000);
}

void MainWindow::save()
{
    if (!m_session.isOpen())
        return;
    flushPendingEdits();
    (void)saveLanguage(m_session.currentLanguage());
}

bool MainWindow::confirmSaveWithErrors(const QString &language)
{
    const QList<Finding> findings = m_session.findings(language);
    const int errors = countBySeverity(findings, Severity::Error);
    if (errors > 0) {
        QStringList firstFew;
        for (const Finding &finding : findings) {
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
            return false;
    }
    return true;
}

bool MainWindow::saveLanguage(const QString &language)
{
    if (!m_session.isDirty(language))
        return true;
    if (!confirmSaveWithErrors(language))
        return false;

    const SongDocument *before = m_session.document(language);
    const QString path = before ? before->path : QString();
    auto saved = m_session.save(language);
    if (!saved && saved.error().kind == Session::SaveError::Kind::Conflict) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("File changed on disk"));
        box.setText(saved.error().message);
        box.setInformativeText(tr("Overwrite only if you are sure the disk changes are no "
                                  "longer needed. Cancel, reload, and compare otherwise."));
        auto *overwrite = box.addButton(tr("Overwrite disk changes"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != overwrite)
            return false;
        saved = m_session.save(language, true);
    }
    if (!saved) {
        QMessageBox::critical(this, tr("Save failed"), saved.error().message);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    updateWindowTitle();
    // A new song only exists in the list once it has been written.
    m_browser->refresh();
    m_browser->showCurrent(m_session.baseDocument() ? m_session.baseDocument()->path
                                                    : m_session.currentPath());
    return true;
}

bool MainWindow::saveAll()
{
    if (!m_session.isOpen())
        return true;
    flushPendingEdits();
    const QStringList dirty = m_session.dirtyLanguages();
    for (const QString &language : dirty) {
        if (!saveLanguage(language))
            return false;
    }
    return true;
}

void MainWindow::reloadFromDisk()
{
    if (!m_session.isOpen())
        return;
    if (!confirmDiscard())
        return;
    // Use the current overlay path when applicable so Session reselects the
    // same language after normalizing it to the sibling song.toml.
    const QString path = m_session.currentPath();
    if (auto opened = m_session.openSong(path); !opened) {
        // openSong loads the replacement family before closing this one, so a
        // failed reload leaves the in-memory edits available for recovery.
        showParseError(this, opened.error());
        return;
    }
    m_planStale = true;
    updateLanguageTabs();
    updateWindowTitle();
    updateTransportDuration();
    m_browser->showCurrent(m_session.baseDocument() ? m_session.baseDocument()->path
                                                    : m_session.currentPath());
    statusBar()->showMessage(tr("Reloaded %1").arg(path), 4000);
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

void MainWindow::downloadLatestCorpus()
{
    const QString target = internalCorpusRoot();
    const bool replacing = QFileInfo::exists(target);
    const bool currentUsesTarget = QDir::cleanPath(m_library.root()) == QDir::cleanPath(target);
    if (currentUsesTarget && !confirmDiscard())
        return;

    QMessageBox warning(this);
    warning.setIcon(QMessageBox::Warning);
    warning.setWindowTitle(tr("Download latest OP-songs?"));
    warning.setText(replacing
            ? tr("This will replace the editor's internal OP-songs directory.")
            : tr("This will create the editor's internal OP-songs directory."));
    warning.setInformativeText(
        tr("Destination:\n%1\n\nThe head of the public main branch will be downloaded from "
           "GitHub into a temporary directory. OPE will reject unsafe archive paths, then "
           "parse every TOML file, check unchanged byte round trips, re-emit every note "
           "token, and run the complete validation rule set before changing this path.%2")
            .arg(target,
                replacing
                    ? tr("\n\nThe existing directory will be moved to a timestamped backup, "
                         "not deleted.")
                    : QString()));
    auto *download = warning.addButton(
        replacing ? tr("Download and replace") : tr("Download"), QMessageBox::AcceptRole);
    warning.addButton(QMessageBox::Cancel);
    warning.setDefaultButton(QMessageBox::Cancel);
    warning.exec();
    if (warning.clickedButton() != download)
        return;

    const QString currentPath = currentUsesTarget && m_session.isOpen()
        ? m_session.currentPath()
        : QString();
    CorpusDownloadDialog dialog(target, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_library.setRoot(target);
    QSettings().setValue(settingsKeyRoot(), target);
    m_browser->refresh();
    if (!currentPath.isEmpty()) {
        if (auto opened = m_session.openSong(currentPath); !opened) {
            showParseError(this, opened.error());
        } else {
            updateLanguageTabs();
            updateWindowTitle();
            updateTransportDuration();
            m_browser->showCurrent(currentPath);
        }
    }
    statusBar()->showMessage(
        tr("Installed validated OP-songs snapshot: %1")
            .arg(dialog.checkSummary().description()),
        10000);
}

void MainWindow::prepareContribution()
{
    if (!m_session.isOpen()) {
        QMessageBox::information(this, tr("Prepare Contribution"),
            tr("Open or create the song you want to contribute first."));
        return;
    }
    flushPendingEdits();

    const SongDocument &doc = m_session.document();
    const QByteArray baseline = m_session.openedBytes();
    const bool newFile = baseline.isEmpty();
    const bool newSong = newFile && !doc.isOverlay;
    if (m_session.isNewFile() && !newSong && QFileInfo::exists(doc.path)) {
        QMessageBox::warning(this, tr("Prepare Contribution"),
            tr("%1 now exists even though this translation was created as a new file. "
               "Reload and reconcile that file before contributing.")
                .arg(doc.path));
        return;
    }
    if (!m_session.isNewFile()) {
        QFile disk(doc.path);
        if (!disk.open(QIODevice::ReadOnly) || disk.readAll() != doc.originalBytes) {
            QMessageBox::warning(this, tr("Prepare Contribution"),
                tr("%1 changed on disk after it was opened or saved. Reload and reconcile "
                   "that change before preparing a contribution.")
                    .arg(doc.path));
            return;
        }
    }

    QByteArray baseBytes;
    if (doc.isOverlay) {
        const SongDocument *base = m_session.baseDocument();
        if (!base) {
            QMessageBox::critical(this, tr("Prepare Contribution"),
                tr("This translation has no open base song.toml."));
            return;
        }
        if (m_session.isDirty(base->language) || m_session.openedBytes(base->language).isEmpty()) {
            QMessageBox::warning(this, tr("Prepare Contribution"),
                tr("The base song is new or has changes in this session. Prepare the base-song "
                   "contribution first, then reopen the accepted baseline before packaging "
                   "this translation."));
            return;
        }
        baseBytes = io::serialize(*base);
    }

    const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString output = QFileDialog::getExistingDirectory(this,
        tr("Choose Where to Create the Contribution Bundle"), suggested);
    if (output.isEmpty())
        return;
    if (isInsideDirectory(output, m_library.root())) {
        QMessageBox::warning(this, tr("Prepare Contribution"),
            tr("Choose a destination outside the OP-songs corpus. Contribution ZIPs and "
               "reports must never become accidental corpus files."));
        return;
    }

    contrib::Request request;
    request.outputParent = output;
    request.workId = doc.workId;
    request.title = doc.title.valueOr(tr("(untitled)"));
    request.language = m_session.currentLanguage();
    request.fileName = QFileInfo(doc.path).fileName();
    request.editorVersion = QCoreApplication::applicationVersion();
    request.proposedToml
        = m_session.isNewFile() ? io::serializeFresh(doc) : io::serialize(doc);
    request.baselineToml = baseline;
    request.baseToml = baseBytes;
    const QString copyrightPath
        = QFileInfo(doc.path).dir().filePath(QStringLiteral("copyright.txt"));
    QFile copyrightFile(copyrightPath);
    if (copyrightFile.open(QIODevice::ReadOnly))
        request.copyrightFile = copyrightFile.readAll();

    const auto prepared = contrib::prepare(request);
    if (!prepared) {
        m_problemsDock->show();
        m_problemsDock->raise();
        QMessageBox::warning(this, tr("Contribution Not Ready"), prepared.error());
        return;
    }

    QMessageBox result(this);
    result.setIcon(QMessageBox::Information);
    result.setWindowTitle(tr("Contribution Bundle Ready"));
    result.setText(tr("The exact proposed TOML passed preflight and was packaged for review."));
    if (prepared->newSong) {
        result.setInformativeText(
            tr("%1\n\nNew song file:\n%2\n\nOpen the GitHub form and drag this "
               "song.toml file into the details field. As a fallback, OPE will copy the "
               "complete file as a TOML code block. The corpus maintainer will assign its "
               "upstream song ID.\n\nOptional review ZIP:\n%3")
                .arg(prepared->checks.description(), prepared->proposedFile,
                    prepared->archive));
    } else {
        result.setInformativeText(
            tr("%1\n\nZIP bundle:\n%2\n\nSHA-256:\n%3\n\nOpen the GitHub form and "
               "drag this ZIP into the details field. OPE will copy the preflight report "
               "to the clipboard for you.")
                .arg(prepared->checks.description(), prepared->archive,
                    prepared->archiveSha256));
    }
    auto *openIssue = result.addButton(tr("Open GitHub Issue"), QMessageBox::AcceptRole);
    auto *openFolder = result.addButton(tr("Open Bundle Folder"), QMessageBox::ActionRole);
    result.addButton(QMessageBox::Close);
    result.setDefaultButton(openIssue);
    result.exec();

    if (result.clickedButton() == openFolder) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(prepared->directory));
        return;
    }
    if (result.clickedButton() != openIssue)
        return;

    QFile report(prepared->reportFile);
    QString clipboardText;
    if (report.open(QIODevice::ReadOnly))
        clipboardText = QString::fromUtf8(report.readAll());
    if (prepared->newSong) {
        if (!clipboardText.isEmpty())
            clipboardText += QStringLiteral("\n");
        clipboardText += tomlCodeBlock(request.proposedToml);
    }
    if (!clipboardText.isEmpty())
        QGuiApplication::clipboard()->setText(clipboardText);

    QUrl url(QStringLiteral("https://github.com/squinky86/OP-songs/issues/new"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("template"), QStringLiteral("song-problem.yml"));
    if (prepared->newSong) {
        const QString identity = tr("New song — %1").arg(request.title);
        query.addQueryItem(
            QStringLiteral("title"), QStringLiteral("[New Song] %1").arg(request.title));
        query.addQueryItem(QStringLiteral("song"), identity);
        query.addQueryItem(QStringLiteral("location"),
            tr("New song submission; proposed file song.toml; language %1")
                .arg(m_session.currentLanguage()));
        query.addQueryItem(QStringLiteral("details"),
            tr("A new song passed OpenPsalm Editor %1 preflight. %2 Drag the generated "
               "song.toml into this field, or paste the copied TOML code block, then explain "
               "the musical/textual source. No song ID is proposed; the corpus maintainer "
               "will assign it.")
                .arg(request.editorVersion, prepared->checks.description()));
    } else {
        const QString identity
            = QStringLiteral("#%1 — %2").arg(doc.workId).arg(request.title);
        query.addQueryItem(
            QStringLiteral("title"), QStringLiteral("[Song] %1").arg(identity));
        query.addQueryItem(QStringLiteral("song"), identity);
        QString songUrl = QStringLiteral("https://openpsalm.com/songs/%1").arg(doc.workId);
        if (m_session.currentLanguage() != i18n::defaultLanguage())
            songUrl += u'/' + m_session.currentLanguage();
        query.addQueryItem(QStringLiteral("song-url"), songUrl);
        query.addQueryItem(QStringLiteral("location"),
            tr("Language %1; proposed file %2")
                .arg(m_session.currentLanguage(), request.fileName));
        query.addQueryItem(QStringLiteral("details"),
            tr("A contribution bundle was prepared by OpenPsalm Editor %1. %2 Bundle "
               "SHA-256: %3. Drag the ZIP into this field, paste the copied preflight report, "
               "and explain the musical/textual source for the change.")
                .arg(request.editorVersion, prepared->checks.description(),
                    prepared->archiveSha256));
    }
    url.setQuery(query);
    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::warning(this, tr("Open GitHub issue"),
            tr("Could not open a web browser. Open this address manually:\n\n%1\n\nThe "
               "submission text is already on the clipboard.")
                .arg(url.toString(QUrl::FullyEncoded)));
    }
}

void MainWindow::reportSongProblem()
{
    QUrl url(QStringLiteral("https://github.com/squinky86/OP-songs/issues/new"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("template"), QStringLiteral("song-problem.yml"));
    if (m_session.isOpen()) {
        flushPendingEdits();
        const SongDocument &doc = m_session.document();
        const QString identity = QStringLiteral("#%1 — %2")
                                     .arg(doc.workId)
                                     .arg(doc.title.valueOr(tr("(untitled)")));
        query.addQueryItem(QStringLiteral("title"), QStringLiteral("[Song] %1").arg(identity));
        query.addQueryItem(QStringLiteral("song"), identity);
        QString location = tr("Language %1; file %2")
                               .arg(m_session.currentLanguage(), QFileInfo(doc.path).fileName());
        const Selection selection = m_session.selection();
        const SongDocument &effective = m_session.effectiveDocument();
        if (selection.isValid() && selection.partIndex < effective.parts.size()) {
            location += tr("; measure %1; %2")
                            .arg(selection.measureIndex + 1)
                            .arg(effective.parts.at(selection.partIndex).name);
        }
        query.addQueryItem(QStringLiteral("location"), location);
        QString songUrl = QStringLiteral("https://openpsalm.com/songs/%1").arg(doc.workId);
        if (m_session.currentLanguage() != i18n::defaultLanguage())
            songUrl += u'/' + m_session.currentLanguage();
        query.addQueryItem(QStringLiteral("song-url"), songUrl);
        query.addQueryItem(QStringLiteral("details"),
            tr("Reported from OpenPsalm Editor %1. Describe what is wrong and what it should "
               "be instead.")
                .arg(QCoreApplication::applicationVersion()));
    }
    url.setQuery(query);
    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::warning(this, tr("Open GitHub issue"),
            tr("Could not open a web browser. Open this address manually:\n\n%1")
                .arg(url.toString(QUrl::FullyEncoded)));
    }
}

bool MainWindow::confirmDiscard()
{
    flushPendingEdits();
    if (!m_session.isDirty())
        return true;
    QStringList files;
    for (const QString &language : m_session.dirtyLanguages()) {
        if (const SongDocument *doc = m_session.document(language))
            files.append(QFileInfo(doc->path).fileName());
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(this,
        tr("Unsaved changes"),
        tr("These files have unsaved changes:\n\n%1\n\nSave all before continuing?")
            .arg(files.join(u'\n')),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        return saveAll() && !m_session.isDirty();
    }
    return true;
}

void MainWindow::flushPendingEdits()
{
    if (m_header)
        m_header->commitPendingEdits();
    if (m_lyrics)
        m_lyrics->commitPendingEdits();
}

bool MainWindow::hasUnsavedWork() const
{
    return m_session.isDirty() || (m_header && m_header->hasPendingEdits())
        || (m_lyrics && m_lyrics->hasPendingEdits());
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
