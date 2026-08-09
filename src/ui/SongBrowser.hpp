// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>

namespace OpenPsalm {

class SongBrowser : public QWidget {
    Q_OBJECT

public:
    explicit SongBrowser(QWidget* parent = nullptr);
    void setSongsDirectory(const QString& dirPath);
    void refresh();

signals:
    void songSelected(const QString& filePath);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void filterSongs(const QString& filter);

private:
    QString m_songsDir;
    QLineEdit* m_searchBox{nullptr};
    QTreeWidget* m_treeWidget{nullptr};
};

} // namespace OpenPsalm
