// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QDockWidget>
#include "core/SongDocument.hpp"
#include "ScoreView.hpp"
#include "NotesEditor.hpp"
#include "LyricsEditor.hpp"
#include "MetadataEditor.hpp"
#include "InspectorWidget.hpp"
#include "DiagnosticsWidget.hpp"
#include "SongBrowser.hpp"

namespace OpenPsalm {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openSong(const QString& filePath);

private slots:
    void newSong();
    void openFileDialog();
    void saveSong();
    void saveSongAs();
    void onDocumentModified();
    void onDocumentLoaded();
    void onDiagnosticsChanged();
    void onDiagnosticClicked(const Diagnostic& diag);
    void showAbout();

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createDocks();
    void updateTitle();

    SongDocument* m_document{nullptr};

    QTabWidget* m_tabWidget{nullptr};
    ScoreView* m_scoreView{nullptr};
    NotesEditor* m_notesEditor{nullptr};
    LyricsEditor* m_lyricsEditor{nullptr};
    MetadataEditor* m_metadataEditor{nullptr};

    SongBrowser* m_songBrowser{nullptr};
    InspectorWidget* m_inspector{nullptr};
    DiagnosticsWidget* m_diagnosticsWidget{nullptr};
};

} // namespace OpenPsalm
