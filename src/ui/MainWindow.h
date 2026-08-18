// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include "app/Session.h"
#include "audio/AudioEngine.h"
#include "core/Library.h"

#include <QByteArray>
#include <QDateTime>
#include <QMainWindow>
#include <QNetworkAccessManager>

QT_BEGIN_NAMESPACE
class QAction;
class QDockWidget;
class QLabel;
class QNetworkReply;
class QSplitter;
class QTabWidget;
class QTimer;
QT_END_NAMESPACE

namespace ope::ui {

class HeaderPanel;
class InspectorPanel;
class LyricsPanel;
class ProblemsPanel;
class ScoreView;
class SongBrowser;
class SourcePanel;
class TransportBar;
class WorkspaceSection;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Open a file directly (a path on the command line).
    void openPath(const QString &path);
    /// Expand and focus a workspace section: 0 score, 1 lyrics, 2 source.
    void selectTab(int index);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildMenus();
    void buildLayout();
    void updateWindowTitle();
    void updateLanguageTabs();
    void refreshPlaybackPlan();
    void updateTransportDuration();
    [[nodiscard]] bool flushPendingEdits();
    [[nodiscard]] bool hasUnsavedWork() const;
    void focusWorkspaceSection(int index);
    void updateProblemsTab();
    void setStructuredEditingBlocked(bool blocked);

    void showBrowser();
    void newSong();
    void addTranslation();
    void save();
    [[nodiscard]] bool saveAll();
    [[nodiscard]] bool saveLanguage(const QString &language);
    [[nodiscard]] bool confirmSaveWithErrors(const QString &language);
    void reloadFromDisk();
    void editPreferences();
    void downloadLatestCorpus();
    void checkCorpusUpdates(bool notifyOnFailure = false);
    void corpusHeadCheckFinished();
    void refreshManagedCorpusStatus();
    void manageCorpusBackups();
    void restoreCorpusBackup(const QString &backup);
    void prepareContribution();
    void reportSongProblem();
    [[nodiscard]] bool confirmDiscard();
    void navigateTo(const Finding &finding);

    Library m_library;
    Session m_session;
    audio::AudioEngine m_audio;

    ScoreView *m_score = nullptr;
    SongBrowser *m_browser = nullptr;
    QDockWidget *m_browserDock = nullptr;
    QDockWidget *m_toolsDock = nullptr;
    LyricsPanel *m_lyrics = nullptr;
    SourcePanel *m_source = nullptr;
    HeaderPanel *m_header = nullptr;
    InspectorPanel *m_inspector = nullptr;
    ProblemsPanel *m_problems = nullptr;
    TransportBar *m_transport = nullptr;
    QSplitter *m_workspace = nullptr;
    WorkspaceSection *m_scoreSection = nullptr;
    WorkspaceSection *m_lyricsSection = nullptr;
    WorkspaceSection *m_sourceSection = nullptr;
    QTabWidget *m_toolsTabs = nullptr;
    int m_problemsTab = -1;
    QTabWidget *m_languageTabs = nullptr;
    QLabel *m_statusSummary = nullptr;
    QAction *m_saveCurrentAction = nullptr;
    QAction *m_saveAllAction = nullptr;
    QAction *m_downloadCorpusAction = nullptr;
    QNetworkAccessManager m_corpusNetwork;
    QNetworkReply *m_corpusHeadReply = nullptr;
    QTimer *m_corpusRecheckTimer = nullptr;
    QByteArray m_corpusHeadResponse;
    QString m_latestCorpusSha;
    QDateTime m_latestCorpusCommitDate;
    QDateTime m_corpusCheckedAt;
    QString m_corpusCheckError;
    bool m_notifyCorpusCheckFailure = false;
    bool m_planStale = true;
};

} // namespace ope::ui
