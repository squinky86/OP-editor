// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include "app/Session.h"
#include "audio/AudioEngine.h"
#include "core/Library.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QDockWidget;
class QLabel;
class QTabWidget;
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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Open a file directly (a path on the command line).
    void openPath(const QString &path);
    /// Select a centre tab by index: 0 score, 1 lyrics, 2 source.
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
    void flushPendingEdits();
    [[nodiscard]] bool hasUnsavedWork() const;

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
    QDockWidget *m_songDock = nullptr;
    QDockWidget *m_problemsDock = nullptr;
    LyricsPanel *m_lyrics = nullptr;
    SourcePanel *m_source = nullptr;
    HeaderPanel *m_header = nullptr;
    InspectorPanel *m_inspector = nullptr;
    ProblemsPanel *m_problems = nullptr;
    TransportBar *m_transport = nullptr;
    QTabWidget *m_centre = nullptr;
    QTabWidget *m_languageTabs = nullptr;
    QLabel *m_statusSummary = nullptr;
    bool m_planStale = true;
};

} // namespace ope::ui
