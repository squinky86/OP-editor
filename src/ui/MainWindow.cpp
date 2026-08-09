// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MainWindow.hpp"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include "../app/Settings.hpp"

namespace OpenPsalm {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_document(new SongDocument(this))
{
    resize(1280, 800);
    setWindowTitle(QStringLiteral("OpenPsalm Editor"));

    // Center tab widget
    m_tabWidget = new QTabWidget(this);
    m_scoreView = new ScoreView(m_document, this);
    m_notesEditor = new NotesEditor(m_document, this);
    m_lyricsEditor = new LyricsEditor(m_document, this);
    m_metadataEditor = new MetadataEditor(m_document, this);

    m_tabWidget->addTab(m_scoreView, QStringLiteral("Score"));
    m_tabWidget->addTab(m_notesEditor, QStringLiteral("Notes (Text)"));
    m_tabWidget->addTab(m_lyricsEditor, QStringLiteral("Lyrics"));
    m_tabWidget->addTab(m_metadataEditor, QStringLiteral("Metadata"));

    setCentralWidget(m_tabWidget);

    createActions();
    createMenus();
    createToolBars();
    createDocks();

    connect(m_document, &SongDocument::documentModified, this, &MainWindow::onDocumentModified);
    connect(m_document, &SongDocument::documentLoaded, this, &MainWindow::onDocumentLoaded);
    connect(m_document, &SongDocument::documentSaved, this, &MainWindow::onDocumentModified);
    connect(m_document, &SongDocument::diagnosticsChanged, this, &MainWindow::onDiagnosticsChanged);

    connect(m_songBrowser, &SongBrowser::songSelected, this, &MainWindow::openSong);
    connect(m_diagnosticsWidget, &DiagnosticsWidget::diagnosticSelected, this, &MainWindow::onDiagnosticClicked);

    statusBar()->showMessage(QStringLiteral("Ready"));
}

void MainWindow::createActions() {
    // Actions are hooked in menus and toolbars
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&New Song"), this, &MainWindow::newSong, QKeySequence::New);
    fileMenu->addAction(QStringLiteral("&Open..."), this, &MainWindow::openFileDialog, QKeySequence::Open);
    fileMenu->addAction(QStringLiteral("&Save"), this, &MainWindow::saveSong, QKeySequence::Save);
    fileMenu->addAction(QStringLiteral("Save &As..."), this, &MainWindow::saveSongAs, QKeySequence::SaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), qApp, &QApplication::quit, QKeySequence::Quit);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    auto* undoAct = m_document->undoStack()->createUndoAction(this, QStringLiteral("&Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    auto* redoAct = m_document->undoStack()->createRedoAction(this, QStringLiteral("&Redo"));
    redoAct->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("&About OpenPsalm Editor"), this, &MainWindow::showAbout);
}

void MainWindow::createToolBars() {
    auto* mainToolBar = addToolBar(QStringLiteral("Main Toolbar"));
    mainToolBar->addAction(QStringLiteral("Open"), this, &MainWindow::openFileDialog);
    mainToolBar->addAction(QStringLiteral("Save"), this, &MainWindow::saveSong);
    mainToolBar->addSeparator();

    auto* undoAct = m_document->undoStack()->createUndoAction(this, QStringLiteral("Undo"));
    auto* redoAct = m_document->undoStack()->createRedoAction(this, QStringLiteral("Redo"));
    mainToolBar->addAction(undoAct);
    mainToolBar->addAction(redoAct);
}

void MainWindow::createDocks() {
    // Left dock: Song Browser
    auto* browserDock = new QDockWidget(QStringLiteral("Song Library"), this);
    browserDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_songBrowser = new SongBrowser(browserDock);
    browserDock->setWidget(m_songBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, browserDock);

    // Set default songs path if present
    m_songBrowser->setSongsDirectory(Settings::openPsalmSongsPath());

    // Right dock: Inspector
    auto* inspectorDock = new QDockWidget(QStringLiteral("Inspector"), this);
    inspectorDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_inspector = new InspectorWidget(m_document, inspectorDock);
    inspectorDock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    // Bottom dock: Diagnostics
    auto* diagDock = new QDockWidget(QStringLiteral("Diagnostics & Issues"), this);
    diagDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_diagnosticsWidget = new DiagnosticsWidget(diagDock);
    diagDock->setWidget(m_diagnosticsWidget);
    addDockWidget(Qt::BottomDockWidgetArea, diagDock);
}

void MainWindow::openSong(const QString& filePath) {
    if (m_document->loadFromFile(filePath)) {
        statusBar()->showMessage(QStringLiteral("Loaded: %1").arg(filePath), 4000);
        updateTitle();
    } else {
        QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("Failed to load song from %1").arg(filePath));
    }
}

void MainWindow::newSong() {
    // Reset to blank default template
    SongData blank;
    blank.title = QStringLiteral("New Song");
    blank.verseCount = 1;
    blank.keySignature = QStringLiteral("C");
    blank.timeSigNumerator = 4;
    blank.timeSigDenominator = 4;
    blank.tempoBpm = 100;
    blank.copyrights = {QStringLiteral("Copyright (C) 2026 Jon Hood, OpenPsalm.com")};

    PartData sPart;
    sPart.name = QStringLiteral("Soprano");
    sPart.choralType = ChoralType::Soprano;
    sPart.clef = Clef::Treble;
    sPart.staffNumber = 1;
    sPart.notesText = QStringLiteral("c'4 d'4 e'4 f'4 | g'1 |");
    blank.parts.insert(sPart.name, sPart);

    m_document->songData() = blank;
    m_document->revalidate();
    m_document->setDirty(false);
    updateTitle();
}

void MainWindow::openFileDialog() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open OpenPsalm TOML Song"),
                                                Settings::openPsalmSongsPath(),
                                                QStringLiteral("OpenPsalm Song (*.toml);;All Files (*)"));
    if (!path.isEmpty()) {
        openSong(path);
    }
}

void MainWindow::saveSong() {
    if (m_document->filePath().isEmpty()) {
        saveSongAs();
        return;
    }
    if (m_document->saveToFile()) {
        statusBar()->showMessage(QStringLiteral("Saved: %1").arg(m_document->filePath()), 4000);
        updateTitle();
    } else {
        QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("Failed to save file."));
    }
}

void MainWindow::saveSongAs() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save OpenPsalm TOML Song"),
                                                m_document->filePath().isEmpty() ? Settings::openPsalmSongsPath() : m_document->filePath(),
                                                QStringLiteral("OpenPsalm Song (*.toml);;All Files (*)"));
    if (!path.isEmpty()) {
        if (m_document->saveToFile(path)) {
            statusBar()->showMessage(QStringLiteral("Saved as: %1").arg(path), 4000);
            updateTitle();
        } else {
            QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("Failed to save file."));
        }
    }
}

void MainWindow::onDocumentModified() {
    updateTitle();
}

void MainWindow::onDocumentLoaded() {
    updateTitle();
}

void MainWindow::onDiagnosticsChanged() {
    m_diagnosticsWidget->setDiagnostics(m_document->diagnostics());
}

void MainWindow::onDiagnosticClicked(const Diagnostic& diag) {
    if (!diag.partName.isEmpty()) {
        m_tabWidget->setCurrentIndex(1); // Switch to Notes (Text)
    }
}

void MainWindow::updateTitle() {
    QString path = m_document->filePath();
    QString title = m_document->songData().title;
    if (title.isEmpty()) title = QStringLiteral("Untitled");

    QString dirtyMarker = m_document->isDirty() ? QStringLiteral("*") : QString();
    QString overlayMarker = m_document->isOverlay() ? QStringLiteral(" [Translation Overlay]") : QString();

    setWindowTitle(QStringLiteral("%1%2%3 - OpenPsalm Editor").arg(title).arg(dirtyMarker).arg(overlayMarker));
}

void MainWindow::showAbout() {
    QMessageBox::about(this, QStringLiteral("About OpenPsalm Editor"),
                       QStringLiteral("<h3>OpenPsalm Editor v0.1.0</h3>"
                                      "<p>Desktop SATB score and TOML editor for the OpenPsalm song corpus.</p>"
                                      "<p><b>Copyright (C) 2026 Jon Hood, OpenPsalm.com</b><br>"
                                      "Licensed under the GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later).</p>"));
}

} // namespace OpenPsalm
