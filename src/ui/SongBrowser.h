// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The songs directory, always on screen.
//
// OPE edits a folder of hymns, so the folder is part of the window rather than
// something behind a file dialog: pick a song, edit it, pick the next one. The
// list is a cache of a directory that git, a text editor, or another checkout
// may change underneath it, which is why Refresh is a button and not a menu
// item three levels down.

#pragma once

#include "core/Library.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace ope::ui {

enum class CorpusUpdateState { Hidden, Checking, Current, UpdateAvailable, CheckFailed };

class SongBrowser : public QWidget {
    Q_OBJECT
public:
    explicit SongBrowser(Library *library, QWidget *parent = nullptr);

    /// Re-read the songs directory and rebuild the list.
    void refresh();
    /// Highlight the row holding `path` without opening it again.
    void showCurrent(const QString &path);
    void focusSearch();
    void setManagedCorpusStatus(CorpusUpdateState state, const QString &summary,
        const QString &toolTip, int backupCount);

Q_SIGNALS:
    void openRequested(const QString &path);
    void newSongRequested();
    void changeFolderRequested();
    void updateCorpusRequested();
    void manageBackupsRequested();

private:
    void repopulate();
    void openItem(QTreeWidgetItem *item);

    Library *m_library = nullptr;
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_corpusStatus = nullptr;
    QPushButton *m_updateCorpus = nullptr;
    QPushButton *m_manageBackups = nullptr;
    QString m_currentPath;
};

} // namespace ope::ui
