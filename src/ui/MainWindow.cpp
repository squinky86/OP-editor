// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "MainWindow.h"

#include "cli/Contribution.h"
#include "core/CorpusSnapshot.h"
#include "CorpusBackupDialog.h"
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
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QUrlQuery>

namespace ope::ui {
namespace {

QString settingsKeyRoot() { return QStringLiteral("songsRoot"); }

constexpr qsizetype MaxHeadResponseBytes = 1024 * 1024;
constexpr int CorpusRecheckMilliseconds = 6 * 60 * 60 * 1000;

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

QString displayDate(const QDateTime &date)
{
    return date.isValid()
        ? QLocale::system().toString(date.toLocalTime(), QLocale::ShortFormat)
        : QObject::tr("unknown date");
}

} // namespace

class WorkspaceSection final : public QWidget {
public:
    WorkspaceSection(const QString &title, const QString &settingsName, QWidget *content,
        QWidget *parent = nullptr)
        : QWidget(parent), m_content(content), m_settingsName(settingsName)
    {
        setObjectName(settingsName + QStringLiteral("WorkspaceSection"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_toggle = new QToolButton(this);
        m_toggle->setObjectName(settingsName + QStringLiteral("WorkspaceToggle"));
        m_toggle->setText(title);
        m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_toggle->setArrowType(Qt::DownArrow);
        m_toggle->setCheckable(true);
        m_toggle->setChecked(true);
        m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_toggle->setStyleSheet(QStringLiteral(
            "QToolButton { text-align: left; font-weight: 600; padding: 5px 8px; "
            "border: 0; border-bottom: 1px solid palette(mid); }"));
        layout->addWidget(m_toggle);
        layout->addWidget(content, 1);
        connect(m_toggle, &QToolButton::toggled, this,
            [this](bool expanded) { setExpanded(expanded); });
        setExpanded(QSettings().value(
            QStringLiteral("workspace/%1Expanded").arg(settingsName), true).toBool());
    }

    void setExpanded(bool expanded)
    {
        if (m_toggle->isChecked() != expanded) {
            m_toggle->blockSignals(true);
            m_toggle->setChecked(expanded);
            m_toggle->blockSignals(false);
        }
        m_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_content->setVisible(expanded);
        setMaximumHeight(expanded ? QWIDGETSIZE_MAX : m_toggle->sizeHint().height());
        setSizePolicy(QSizePolicy::Preferred,
            expanded ? QSizePolicy::Expanding : QSizePolicy::Fixed);
        QSettings().setValue(
            QStringLiteral("workspace/%1Expanded").arg(m_settingsName), expanded);
        updateGeometry();
    }

private:
    QWidget *m_content = nullptr;
    QString m_settingsName;
    QToolButton *m_toggle = nullptr;
};

class AlertTabBar final : public QTabBar {
public:
    using QTabBar::QTabBar;

    void setAlert(int index, QColor background, QColor foreground)
    {
        if (background.isValid())
            m_alerts.insert(index, { background, foreground });
        else
            m_alerts.remove(index);
        update(tabRect(index));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        for (int index = 0; index < count(); ++index) {
            QStyleOptionTab option;
            initStyleOption(&option, index);
            painter.drawControl(QStyle::CE_TabBarTabShape, option);
            const auto found = m_alerts.constFind(index);
            if (found != m_alerts.constEnd()) {
                QColor background = found->first;
                if (!(option.state & QStyle::State_Selected))
                    background.setAlpha(215);
                painter.fillRect(tabRect(index).adjusted(2, 2, -2, -1), background);
                option.palette.setColor(QPalette::ButtonText, found->second);
                option.palette.setColor(QPalette::WindowText, found->second);
                option.palette.setColor(QPalette::Text, found->second);
            }
            painter.drawControl(QStyle::CE_TabBarTabLabel, option);
        }
    }

private:
    QHash<int, QPair<QColor, QColor>> m_alerts;
};

class DetailsTabWidget final : public QTabWidget {
public:
    explicit DetailsTabWidget(QWidget *parent = nullptr) : QTabWidget(parent)
    {
        setTabBar(new AlertTabBar(this));
    }
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_session(this), m_audio(this)
{
    QSettings settings;
    m_library.setRoot(settings.value(settingsKeyRoot()).toString());
    if (!m_library.root().isEmpty())
        m_library.rescan();

    buildLayout();
    buildMenus();
    m_corpusRecheckTimer = new QTimer(this);
    m_corpusRecheckTimer->setSingleShot(true);
    m_corpusRecheckTimer->setInterval(CorpusRecheckMilliseconds);
    connect(m_corpusRecheckTimer, &QTimer::timeout, this,
        [this] { checkCorpusUpdates(); });
    updateWindowTitle();

    if (!m_audio.isAvailable())
        m_transport->setUnavailable(m_audio.unavailableReason());

    connect(&m_session, &Session::documentChanged, this, [this] {
        m_planStale = true;
        updateWindowTitle();
        m_statusSummary->setText(m_problems->summary());
        updateProblemsTab();
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
    connect(m_source, &SourcePanel::pendingEditsChanged, this, pendingChanged);

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
    resizeDocks({ m_browserDock, m_toolsDock }, { 270, 350 }, Qt::Horizontal);
    QTimer::singleShot(0, this, [this] {
        const QVariantList stored = QSettings().value(QStringLiteral("workspace/sizes")).toList();
        if (stored.size() == 3) {
            m_workspace->setSizes(
                { stored.at(0).toInt(), stored.at(1).toInt(), stored.at(2).toInt() });
        } else {
            m_workspace->setSizes({ 460, 280, 180 });
        }
    });
    refreshManagedCorpusStatus();
    if (QFileInfo::exists(internalCorpusRoot()))
        QTimer::singleShot(0, this, [this] { checkCorpusUpdates(); });
}

void MainWindow::buildLayout()
{
    m_score = new ScoreView(&m_session, this);
    m_lyrics = new LyricsPanel(&m_session, this);
    m_source = new SourcePanel(&m_session, this);

    m_workspace = new QSplitter(Qt::Vertical, this);
    m_workspace->setObjectName(QStringLiteral("workspaceSplitter"));
    m_workspace->setChildrenCollapsible(false);
    m_scoreSection = new WorkspaceSection(tr("Score"), QStringLiteral("score"), m_score,
        m_workspace);
    m_lyricsSection = new WorkspaceSection(tr("Lyrics"), QStringLiteral("lyrics"), m_lyrics,
        m_workspace);
    m_sourceSection = new WorkspaceSection(tr("Source"), QStringLiteral("source"), m_source,
        m_workspace);
    m_workspace->addWidget(m_scoreSection);
    m_workspace->addWidget(m_lyricsSection);
    m_workspace->addWidget(m_sourceSection);
    m_workspace->setStretchFactor(0, 5);
    m_workspace->setStretchFactor(1, 3);
    m_workspace->setStretchFactor(2, 2);
    connect(m_workspace, &QSplitter::splitterMoved, this, [this] {
        QVariantList stored;
        for (const int size : m_workspace->sizes())
            stored.append(size);
        QSettings().setValue(QStringLiteral("workspace/sizes"), stored);
    });

    m_languageTabs = new QTabWidget(this);
    m_languageTabs->setDocumentMode(true);
    m_languageTabs->setTabPosition(QTabWidget::North);
    m_languageTabs->hide();

    m_transport = new TransportBar(&m_session, this);

    auto *central = new QWidget(this);
    // Keep the score usable when the top-level window is snapped narrow. The
    // side docks yield their preferred widths before consuming this workspace.
    central->setMinimumWidth(300);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_languageTabs);
    layout->addWidget(m_workspace, 1);
    auto *transportScroll = new QScrollArea(central);
    transportScroll->setObjectName(QStringLiteral("transportScroll"));
    transportScroll->setWidgetResizable(true);
    transportScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    transportScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    transportScroll->setFrameShape(QFrame::NoFrame);
    transportScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    transportScroll->setFixedHeight(m_transport->sizeHint().height()
        + style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    transportScroll->setWidget(m_transport);
    layout->addWidget(transportScroll);
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
    connect(m_browser, &SongBrowser::updateCorpusRequested, this, [this] {
        if (m_latestCorpusSha.isEmpty() && !m_corpusCheckError.isEmpty())
            checkCorpusUpdates(true);
        else
            downloadLatestCorpus();
    });
    connect(m_browser, &SongBrowser::manageBackupsRequested, this,
        &MainWindow::manageCorpusBackups);

    m_toolsTabs = new DetailsTabWidget(this);
    m_toolsTabs->setObjectName(QStringLiteral("detailsTabs"));
    m_toolsTabs->setDocumentMode(true);
    m_header = new HeaderPanel(&m_session, m_toolsTabs);
    m_inspector = new InspectorPanel(&m_session, this);
    m_problems = new ProblemsPanel(&m_session, m_toolsTabs);
    m_toolsTabs->addTab(m_header, tr("Song"));
    m_toolsTabs->addTab(m_inspector, tr("Inspector"));
    m_problemsTab = m_toolsTabs->addTab(m_problems, tr("Problems"));
    m_toolsDock = new QDockWidget(tr("Details"), this);
    m_toolsDock->setObjectName(QStringLiteral("detailsDock"));
    m_toolsDock->setWidget(m_toolsTabs);
    addDockWidget(Qt::RightDockWidgetArea, m_toolsDock);
    updateProblemsTab();

    m_statusSummary = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusSummary);

    connect(m_problems, &ProblemsPanel::navigate, this, &MainWindow::navigateTo);
    connect(m_score, &ScoreView::statusMessage, this,
        [this](const QString &message) { statusBar()->showMessage(message, 4000); });
    connect(m_lyrics, &LyricsPanel::statusMessage, this,
        [this](const QString &message) { statusBar()->showMessage(message, 4000); });
    connect(m_score, &ScoreView::requestLyricEdit, this, [this](const QString &part, int slot) {
        m_lyricsSection->setExpanded(true);
        m_lyrics->focusSlot(part, slot);
    });
    connect(m_source, &SourcePanel::editingActivated, this, [this] {
        m_header->commitPendingEdits();
        m_lyrics->commitPendingEdits();
    });
    connect(m_source, &SourcePanel::structuredEditingBlocked, this,
        &MainWindow::setStructuredEditingBlocked);
    connect(m_source, &SourcePanel::sourceErrorChanged, this,
        [this](const QString &message) {
            m_problems->setSourceError(message);
            m_statusSummary->setText(m_problems->summary());
            updateProblemsTab();
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
            if (!flushPendingEdits()) {
                updateLanguageTabs();
                m_sourceSection->setExpanded(true);
                m_source->focusEditor();
                return;
            }
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
    m_downloadCorpusAction = fileMenu->addAction(tr("Download &Latest OP-songs…"), this,
        &MainWindow::downloadLatestCorpus);
    fileMenu->addAction(tr("Manage OP-songs &Backups…"), this,
        &MainWindow::manageCorpusBackups);
    fileMenu->addAction(tr("&New Song…"), QKeySequence::New, this, &MainWindow::newSong);
    fileMenu->addAction(tr("Add &Translation…"), this, &MainWindow::addTranslation);
    fileMenu->addAction(tr("Prepare &Contribution…"), this,
        &MainWindow::prepareContribution);
    fileMenu->addSeparator();
    m_saveCurrentAction
        = fileMenu->addAction(tr("&Save Current"), QKeySequence::Save, this, &MainWindow::save);
    m_saveAllAction = fileMenu->addAction(tr("Save &All"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, [this] { (void)saveAll(); });
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
        if (m_source->hasPendingEdits()) {
            m_source->undoPendingEdit();
            return;
        }
        if (!flushPendingEdits())
            return;
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
        if (m_source->hasPendingEdits()) {
            m_source->redoPendingEdit();
            return;
        }
        if (!flushPendingEdits())
            return;
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
        [this] { focusWorkspaceSection(0); });
    viewMenu->addAction(tr("&Lyrics"), QKeySequence(Qt::CTRL | Qt::Key_2), this,
        [this] { focusWorkspaceSection(1); });
    viewMenu->addAction(tr("So&urce"), QKeySequence(Qt::CTRL | Qt::Key_3), this,
        [this] { focusWorkspaceSection(2); });

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
            "<b>Score and Lyrics panes</b><br>notes, text, phrase_breaks, "
            "optional_phrase_breaks, and non_breaking_phrase_breaks.<br><br>"
            "<b>Inspector dock</b><br>choral_type, clef, staff_number, "
            "splice_lyrics_into, suppress_verses, and suppress_verses_when.<br><br>"
            "<b>Translations</b><br>File → Add Translation creates song_LANG.toml safely. "
            "The language is its filename suffix.<br><br>"
            "The editable <b>Source pane</b> handles advanced or newly introduced TOML "
            "fields directly. Valid source changes update every structured pane; invalid "
            "TOML pauses structured editing and Save until it is fixed or reverted. "
            "Unknown TOML is preserved byte-for-byte."));
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
               "F  fermata &nbsp; K  staccato &nbsp; A  accent &nbsp; M  marcato<br>"
               "C  @c &nbsp; E  @e<br>"
               "Ins / Del  insert / delete a note<br>"
               "Ctrl+B  phrase break here &nbsp; Ctrl+Shift+B  optional &nbsp; "
               "Alt+B  non-breaking<br>"
               "Ctrl+Enter  copy this marking to every sounding voice<br>"
               "Ctrl+wheel  zoom"
               "<br><br><b>Anywhere</b><br>"
               "Space  play / pause &nbsp; F5  re-read the songs folder<br>"
               "Ctrl+O or Ctrl+L  jump to the song list<br>"
               "Ctrl+1 / Ctrl+2 / Ctrl+3  expand and focus score / lyrics / source"));
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
                || (m_lyrics && m_lyrics->hasPendingEdits())
                || (m_source && m_source->hasPendingEdits()));
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
    focusWorkspaceSection(index);
}

void MainWindow::focusWorkspaceSection(int index)
{
    WorkspaceSection *section = index == 0 ? m_scoreSection
        : index == 1                         ? m_lyricsSection
        : index == 2                         ? m_sourceSection
                                             : nullptr;
    if (section)
        section->setExpanded(true);
    if (index == 0)
        m_score->setFocus(Qt::ShortcutFocusReason);
    else if (index == 1)
        m_lyrics->focusEditor();
    else if (index == 2)
        m_source->focusEditor();
}

void MainWindow::updateProblemsTab()
{
    if (!m_toolsTabs || m_problemsTab < 0)
        return;
    const QList<Finding> &findings = m_session.findings();
    const int errors = countBySeverity(findings, Severity::Error)
        + (m_source && m_source->hasParseError() ? 1 : 0);
    const int warnings = countBySeverity(findings, Severity::Warning);
    const int infos = countBySeverity(findings, Severity::Info);
    const int total = findings.size() + (m_source && m_source->hasParseError() ? 1 : 0);
    m_toolsTabs->setTabText(m_problemsTab,
        total > 0 ? tr("Problems (%1)").arg(total) : tr("Problems"));
    m_toolsTabs->setTabToolTip(m_problemsTab,
        tr("%1 error(s), %2 warning(s), %3 information item(s)")
            .arg(errors)
            .arg(warnings)
            .arg(infos));
    auto *tabs = static_cast<AlertTabBar *>(m_toolsTabs->tabBar());
    if (errors > 0) {
        tabs->setAlert(m_problemsTab, QColor(0xd1, 0x24, 0x2f), Qt::white);
    } else if (warnings > 0) {
        tabs->setAlert(m_problemsTab, QColor(0xd9, 0x77, 0x06), Qt::white);
    } else if (infos > 0) {
        tabs->setAlert(m_problemsTab, QColor(0xf0, 0xc8, 0x4b), QColor(0x20, 0x21, 0x24));
    } else {
        tabs->setAlert(m_problemsTab, {}, {});
    }
}

void MainWindow::setStructuredEditingBlocked(bool blocked)
{
    m_score->setEnabled(!blocked);
    m_lyrics->setEnabled(!blocked);
    m_header->setEnabled(!blocked);
    m_inspector->setEnabled(!blocked);
    if (m_saveCurrentAction)
        m_saveCurrentAction->setEnabled(!blocked);
    if (m_saveAllAction)
        m_saveAllAction->setEnabled(!blocked);
    if (blocked)
        statusBar()->showMessage(
            tr("Finish or revert the Source edit before using structured editors or Save."));
    else
        statusBar()->clearMessage();
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
    if (!flushPendingEdits())
        return;
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
    if (!flushPendingEdits())
        return;
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
    if (!flushPendingEdits())
        return false;
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
    refreshManagedCorpusStatus();
    if (QDir::cleanPath(root) == QDir::cleanPath(internalCorpusRoot()))
        checkCorpusUpdates();
}

void MainWindow::checkCorpusUpdates(bool notifyOnFailure)
{
    const QString target = internalCorpusRoot();
    const bool managedSelected
        = QDir::cleanPath(m_library.root()) == QDir::cleanPath(target);
    if (!QFileInfo::exists(target) || !managedSelected) {
        if (m_corpusRecheckTimer)
            m_corpusRecheckTimer->stop();
        refreshManagedCorpusStatus();
        return;
    }
    if (m_corpusHeadReply) {
        m_notifyCorpusCheckFailure |= notifyOnFailure;
        return;
    }
    if (m_corpusRecheckTimer)
        m_corpusRecheckTimer->stop();
    m_notifyCorpusCheckFailure = notifyOnFailure;
    m_corpusHeadResponse.clear();
    m_corpusCheckError.clear();
    m_latestCorpusSha.clear();
    m_latestCorpusCommitDate = {};
    m_corpusCheckedAt = {};
    m_corpusHeadReply = m_corpusNetwork.get(corpusHeadRequest());
    refreshManagedCorpusStatus();
    connect(m_corpusHeadReply, &QIODevice::readyRead, this, [this] {
        m_corpusHeadResponse.append(m_corpusHeadReply->readAll());
        if (m_corpusHeadResponse.size() > MaxHeadResponseBytes) {
            m_corpusCheckError = tr("GitHub commit metadata exceeded the safety limit.");
            m_corpusHeadReply->abort();
        }
    });
    connect(m_corpusHeadReply, &QNetworkReply::finished, this,
        &MainWindow::corpusHeadCheckFinished);
}

void MainWindow::corpusHeadCheckFinished()
{
    m_corpusHeadResponse.append(m_corpusHeadReply->readAll());
    if (m_corpusHeadResponse.size() > MaxHeadResponseBytes && m_corpusCheckError.isEmpty())
        m_corpusCheckError = tr("GitHub commit metadata exceeded the safety limit.");
    const QNetworkReply::NetworkError error = m_corpusHeadReply->error();
    const QString networkError = m_corpusHeadReply->errorString();
    m_corpusHeadReply->deleteLater();
    m_corpusHeadReply = nullptr;
    if (m_corpusCheckError.isEmpty() && error != QNetworkReply::NoError)
        m_corpusCheckError = tr("Could not check OP-songs HEAD: %1").arg(networkError);
    if (m_corpusCheckError.isEmpty()) {
        const auto parsed
            = parseCorpusHeadResponse(m_corpusHeadResponse, QDateTime::currentDateTimeUtc());
        if (!parsed) {
            m_corpusCheckError = parsed.error();
        } else {
            m_latestCorpusSha = parsed->sha;
            m_latestCorpusCommitDate = parsed->committedAt;
            m_corpusCheckedAt = parsed->checkedAt;
            const auto installed = corpus::readSnapshot(internalCorpusRoot());
            if (installed
                && installed->commitSha.compare(parsed->sha, Qt::CaseInsensitive) == 0) {
                if (auto marked = corpus::markSnapshotCurrent(internalCorpusRoot(), parsed->sha,
                        parsed->committedAt, parsed->checkedAt);
                    !marked) {
                    m_corpusCheckError = tr("HEAD was checked, but freshness metadata could not "
                                            "be saved: %1")
                                             .arg(marked.error());
                }
            }
        }
    }
    m_corpusHeadResponse.clear();
    refreshManagedCorpusStatus();
    if (m_notifyCorpusCheckFailure && !m_corpusCheckError.isEmpty()) {
        QMessageBox::warning(this, tr("Could not check for corpus updates"),
            tr("%1\n\nThe installed corpus was not changed.").arg(m_corpusCheckError));
    }
    m_notifyCorpusCheckFailure = false;
    const bool managedSelected = QDir::cleanPath(m_library.root())
        == QDir::cleanPath(internalCorpusRoot());
    if (managedSelected && QFileInfo::exists(internalCorpusRoot())
        && m_corpusRecheckTimer) {
        m_corpusRecheckTimer->start();
    }
}

void MainWindow::refreshManagedCorpusStatus()
{
    const QString target = internalCorpusRoot();
    const bool managedSelected
        = QDir::cleanPath(m_library.root()) == QDir::cleanPath(target);
    if (!managedSelected) {
        m_browser->setManagedCorpusStatus(CorpusUpdateState::Hidden, {}, {}, 0);
        if (m_downloadCorpusAction)
            m_downloadCorpusAction->setText(tr("Download &Latest OP-songs…"));
        return;
    }

    const int backups = corpus::backupDirectories(target).size();
    const auto snapshot = corpus::readSnapshot(target);
    QString installed = tr("Installed snapshot has no resolved commit metadata.");
    QString details = snapshot ? QString() : snapshot.error();
    if (snapshot && snapshot->hasResolvedCommit()) {
        installed = tr("Installed %1 • commit %2")
                        .arg(corpus::abbreviatedSha(snapshot->commitSha),
                            displayDate(snapshot->commitDate));
        if (snapshot->currentAsOf.isValid())
            installed += tr(" • current as of %1").arg(displayDate(snapshot->currentAsOf));
        details = tr("Installed commit: %1\nCommitted: %2\nCurrent as of: %3")
                      .arg(snapshot->commitSha, displayDate(snapshot->commitDate),
                          displayDate(snapshot->currentAsOf));
    }

    if (m_corpusHeadReply) {
        m_browser->setManagedCorpusStatus(
            CorpusUpdateState::Checking, installed, tr("Checking OP-songs HEAD…"), backups);
        return;
    }
    if (!m_latestCorpusSha.isEmpty()) {
        const bool current = snapshot
            && snapshot->commitSha.compare(m_latestCorpusSha, Qt::CaseInsensitive) == 0;
        if (current) {
            const QString summary = tr("Installed %1 • current as of %2")
                                        .arg(corpus::abbreviatedSha(m_latestCorpusSha),
                                            displayDate(m_corpusCheckedAt));
            const QString tip = tr("Installed commit: %1\nCommitted: %2\nCurrent as of: %3")
                                    .arg(m_latestCorpusSha,
                                        displayDate(m_latestCorpusCommitDate),
                                        displayDate(m_corpusCheckedAt));
            m_browser->setManagedCorpusStatus(
                CorpusUpdateState::Current, summary, tip, backups);
            if (m_downloadCorpusAction)
                m_downloadCorpusAction->setText(tr("Re-download Current OP-songs…"));
            return;
        }
        const QString summary = snapshot && snapshot->hasResolvedCommit()
            ? tr("Update available: %1 → %2 • checked %3")
                  .arg(corpus::abbreviatedSha(snapshot->commitSha),
                      corpus::abbreviatedSha(m_latestCorpusSha), displayDate(m_corpusCheckedAt))
            : tr("Update recommended • latest %1 • checked %2")
                  .arg(corpus::abbreviatedSha(m_latestCorpusSha),
                      displayDate(m_corpusCheckedAt));
        details += tr("\nLatest commit: %1\nLatest commit date: %2\nChecked: %3")
                       .arg(m_latestCorpusSha, displayDate(m_latestCorpusCommitDate),
                           displayDate(m_corpusCheckedAt));
        m_browser->setManagedCorpusStatus(
            CorpusUpdateState::UpdateAvailable, summary, details, backups);
        if (m_downloadCorpusAction)
            m_downloadCorpusAction->setText(tr("&Update OP-songs…"));
        return;
    }

    QString summary = installed;
    if (!m_corpusCheckError.isEmpty()) {
        summary += tr(" • update check unavailable");
        details += u'\n' + m_corpusCheckError;
    }
    m_browser->setManagedCorpusStatus(
        CorpusUpdateState::CheckFailed, summary, details, backups);
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
    QString installedDetails;
    if (const auto snapshot = corpus::readSnapshot(target);
        snapshot && snapshot->hasResolvedCommit()) {
        installedDetails = tr("\n\nInstalled commit: %1\nCommitted: %2\nCurrent as of: %3")
                               .arg(snapshot->commitSha, displayDate(snapshot->commitDate),
                                   displayDate(snapshot->currentAsOf));
    }
    warning.setInformativeText(
        tr("Destination:\n%1\n\nThe head of the public main branch will be downloaded from "
           "GitHub. OPE first resolves the exact commit SHA, downloads that immutable "
           "archive into a temporary directory, rejects unsafe archive paths, then "
           "parse every TOML file, check unchanged byte round trips, re-emit every note "
           "token, and run the complete validation rule set before changing this path.%2%3")
            .arg(target,
                replacing
                    ? tr("\n\nThe existing directory will be moved to a timestamped backup, "
                         "not deleted.")
                    : QString(),
                installedDetails));
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
    m_latestCorpusSha = dialog.resolvedHead().sha;
    m_latestCorpusCommitDate = dialog.resolvedHead().committedAt;
    m_corpusCheckedAt = dialog.resolvedHead().checkedAt;
    m_corpusCheckError.clear();
    refreshManagedCorpusStatus();
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

void MainWindow::manageCorpusBackups()
{
    const QString target = internalCorpusRoot();
    CorpusBackupDialog dialog(target, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedBackup().isEmpty())
        restoreCorpusBackup(dialog.selectedBackup());
    refreshManagedCorpusStatus();
}

void MainWindow::restoreCorpusBackup(const QString &backup)
{
    const QString target = internalCorpusRoot();
    const bool currentUsesTarget
        = QDir::cleanPath(m_library.root()) == QDir::cleanPath(target);
    if (currentUsesTarget && !confirmDiscard())
        return;
    if (!corpus::backupDirectories(target).contains(QFileInfo(backup).absoluteFilePath())) {
        QMessageBox::critical(this, tr("Unsafe backup path"),
            tr("The selected path is not a managed OP-songs backup:\n\n%1").arg(backup));
        return;
    }
    if (QMessageBox::question(this, tr("Restore this corpus backup?"),
            tr("OPE will validate every song in:\n\n%1\n\nIf validation succeeds, this "
               "backup becomes the managed corpus and the current corpus is retained as a "
               "new timestamped backup.")
                .arg(backup),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes) {
        return;
    }

    QProgressDialog progress(tr("Validating backup…"), tr("Cancel"), 0, 0, this);
    progress.setWindowTitle(tr("Restore OP-songs Backup"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    cli::Options options;
    options.root = backup;
    options.warnings = true;
    options.info = true;
    options.progress = [&progress](int completed, int total, const QString &path) {
        progress.setRange(0, total);
        progress.setValue(completed);
        progress.setLabelText(QObject::tr("Checking %1 of %2:\n%3")
                                  .arg(completed)
                                  .arg(total)
                                  .arg(path));
    };
    options.cancelled = [&progress] {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        return progress.wasCanceled();
    };
    const cli::CheckSummary checks = cli::check(options);
    if (checks.cancelled)
        return;
    if (!checks.passed()) {
        QMessageBox::warning(this, tr("Backup failed validation"),
            tr("The managed corpus was not changed.\n\n%1").arg(checks.description()));
        return;
    }

    const QString currentPath = currentUsesTarget && m_session.isOpen()
        ? m_session.currentPath()
        : QString();
    const auto installed = corpus::installValidatedSnapshot(backup, target);
    if (!installed) {
        QMessageBox::critical(this, tr("Could not restore backup"), installed.error());
        return;
    }
    m_library.setRoot(target);
    QSettings().setValue(settingsKeyRoot(), target);
    m_browser->refresh();
    if (!currentPath.isEmpty() && QFileInfo::exists(currentPath)) {
        if (auto opened = m_session.openSong(currentPath); !opened) {
            showParseError(this, opened.error());
        } else {
            m_browser->showCurrent(currentPath);
            updateLanguageTabs();
            updateWindowTitle();
            updateTransportDuration();
        }
    } else if (currentUsesTarget && m_session.isOpen()) {
        m_session.close();
        updateLanguageTabs();
        updateWindowTitle();
        updateTransportDuration();
    }
    m_latestCorpusSha.clear();
    m_latestCorpusCommitDate = {};
    m_corpusCheckedAt = {};
    m_corpusCheckError.clear();
    refreshManagedCorpusStatus();
    checkCorpusUpdates();
    QMessageBox::information(this, tr("Backup restored"),
        tr("The backup passed validation and is now active.\n\n%1\n\nThe corpus it "
           "replaced was retained at:\n%2")
            .arg(checks.description(), installed->backup));
}

void MainWindow::prepareContribution()
{
    if (!m_session.isOpen()) {
        QMessageBox::information(this, tr("Prepare Contribution"),
            tr("Open or create the song you want to contribute first."));
        return;
    }
    if (!flushPendingEdits())
        return;

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
        if (!disk.open(QIODevice::ReadOnly) || disk.readAll() != m_session.diskBytes()) {
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
    request.proposedToml = m_session.currentBytes();
    request.baselineToml = baseline;
    request.baseToml = baseBytes;
    const QString copyrightPath
        = QFileInfo(doc.path).dir().filePath(QStringLiteral("copyright.txt"));
    QFile copyrightFile(copyrightPath);
    if (copyrightFile.open(QIODevice::ReadOnly))
        request.copyrightFile = copyrightFile.readAll();

    const auto prepared = contrib::prepare(request);
    if (!prepared) {
        m_toolsDock->show();
        m_toolsDock->raise();
        m_toolsTabs->setCurrentIndex(m_problemsTab);
        QMessageBox::warning(this, tr("Contribution Not Ready"), prepared.error());
        return;
    }

    QMessageBox result(this);
    result.setIcon(prepared->checks.warnings > 0 ? QMessageBox::Warning
                                                 : QMessageBox::Information);
    result.setWindowTitle(tr("Contribution Bundle Ready"));
    result.setText(prepared->checks.warnings > 0
            ? tr("The exact proposed TOML passed every blocking check with %n warning(s). "
                 "Review and explain them before submitting.",
                nullptr, prepared->checks.warnings)
            : tr("The exact proposed TOML passed preflight and was packaged for review."));
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
        if (!flushPendingEdits())
            return;
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
    if (!flushPendingEdits()) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(this,
            tr("Invalid source draft"),
            tr("The Source pane contains TOML that cannot update the score or lyrics. "
               "Discard that source draft and continue?"),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Discard)
            return false;
        m_source->discardPendingEdits();
    }
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

bool MainWindow::flushPendingEdits()
{
    if (m_header)
        m_header->commitPendingEdits();
    if (m_lyrics)
        m_lyrics->commitPendingEdits();
    if (m_source && !m_source->commitPendingEdits()) {
        m_sourceSection->setExpanded(true);
        m_source->focusEditor();
        statusBar()->showMessage(tr("Fix or revert the invalid TOML source before continuing."));
        return false;
    }
    return true;
}

bool MainWindow::hasUnsavedWork() const
{
    return m_session.isDirty() || (m_header && m_header->hasPendingEdits())
        || (m_lyrics && m_lyrics->hasPendingEdits())
        || (m_source && m_source->hasPendingEdits());
}

void MainWindow::navigateTo(const Finding &finding)
{
    const SongDocument &doc = m_session.effectiveDocument();
    if (!finding.lyricKey.isEmpty() && finding.measure < 0) {
        m_lyricsSection->setExpanded(true);
        m_lyrics->focusSlot(finding.partName, std::max(0, finding.slot));
        return;
    }
    if (finding.measure > 0) {
        int partIndex = 0;
        for (int i = 0; i < doc.parts.size(); ++i) {
            if (doc.parts.at(i).name == finding.partName)
                partIndex = i;
        }
        m_scoreSection->setExpanded(true);
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
