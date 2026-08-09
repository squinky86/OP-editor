// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SongBrowser.hpp"
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>

namespace OpenPsalm {

SongBrowser::SongBrowser(QWidget* parent)
    : QWidget(parent),
      m_searchBox(new QLineEdit(this)),
      m_treeWidget(new QTreeWidget(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_searchBox->setPlaceholderText(QStringLiteral("Search songs by ID or name..."));
    m_treeWidget->setHeaderLabels({QStringLiteral("Song / Overlay"), QStringLiteral("File")});
    m_treeWidget->header()->setStretchLastSection(true);

    layout->addWidget(m_searchBox);
    layout->addWidget(m_treeWidget);

    connect(m_searchBox, &QLineEdit::textChanged, this, &SongBrowser::filterSongs);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &SongBrowser::onItemDoubleClicked);
}

void SongBrowser::setSongsDirectory(const QString& dirPath) {
    m_songsDir = dirPath;
    refresh();
}

void SongBrowser::refresh() {
    m_treeWidget->clear();
    if (m_songsDir.isEmpty()) return;

    QDir dir(m_songsDir);
    if (!dir.exists()) return;

    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // Sort subdirectories numerically if integer ID
    std::sort(subdirs.begin(), subdirs.end(), [](const QString& a, const QString& b) {
        bool okA = false, okB = false;
        int idA = a.toInt(&okA);
        int idB = b.toInt(&okB);
        if (okA && okB) return idA < idB;
        return a < b;
    });

    for (const QString& sub : subdirs) {
        QDir songDir(dir.filePath(sub));
        QString baseFile = songDir.filePath(QStringLiteral("song.toml"));
        if (QFileInfo::exists(baseFile)) {
            auto* parentItem = new QTreeWidgetItem(m_treeWidget);
            parentItem->setText(0, QStringLiteral("Song %1").arg(sub));
            parentItem->setText(1, QStringLiteral("song.toml"));
            parentItem->setData(0, Qt::UserRole, baseFile);

            // Check for translation overlays e.g. song_es.toml
            QStringList overlays = songDir.entryList({QStringLiteral("song_*.toml")}, QDir::Files);
            for (const QString& ov : overlays) {
                auto* childItem = new QTreeWidgetItem(parentItem);
                childItem->setText(0, QStringLiteral("  Overlay: %1").arg(ov));
                childItem->setText(1, ov);
                childItem->setData(0, Qt::UserRole, songDir.filePath(ov));
            }
        }
    }
}

void SongBrowser::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit songSelected(filePath);
    }
}

void SongBrowser::filterSongs(const QString& filter) {
    QString term = filter.trimmed().toLower();
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        auto* item = m_treeWidget->topLevelItem(i);
        bool match = item->text(0).toLower().contains(term) || item->text(1).toLower().contains(term);
        item->setHidden(!match && !term.isEmpty());
    }
}

} // namespace OpenPsalm
