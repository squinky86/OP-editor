// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "SongBrowser.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

class SongTreeItem final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem &other) const override
    {
        if (treeWidget() && treeWidget()->sortColumn() == 0)
            return text(0).toInt() < other.text(0).toInt();
        return QTreeWidgetItem::operator<(other);
    }
};

} // namespace

namespace ope::ui {

SongBrowser::SongBrowser(Library *library, QWidget *parent)
    : QWidget(parent), m_library(library)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(4);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search title, subtitle, or number"));
    m_search->setClearButtonEnabled(true);
    searchRow->addWidget(m_search, 1);

    auto *reload = new QToolButton(this);
    reload->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    reload->setText(tr("Refresh"));
    reload->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    reload->setToolTip(tr("Re-read the songs folder from disk (F5)"));
    reload->setShortcut(QKeySequence(Qt::Key_F5));
    searchRow->addWidget(reload);
    layout->addLayout(searchRow);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({ tr("#"), tr("Title"), tr("Lang") });
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->setSortingEnabled(true);
    m_tree->sortItems(0, Qt::DescendingOrder);
    layout->addWidget(m_tree, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    auto *newSong = new QPushButton(tr("New song…"), this);
    newSong->setToolTip(tr("Create a song in an automatically selected local draft folder"));
    auto *folder = new QPushButton(tr("Folder…"), this);
    folder->setToolTip(tr("Point the editor at a different OP-songs checkout"));
    buttons->addWidget(newSong);
    buttons->addWidget(folder);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto *corpusButtons = new QHBoxLayout;
    corpusButtons->setSpacing(4);
    m_updateCorpus = new QPushButton(tr("Check OP-songs…"), this);
    m_manageBackups = new QPushButton(tr("Backups…"), this);
    corpusButtons->addWidget(m_updateCorpus, 1);
    corpusButtons->addWidget(m_manageBackups);
    layout->addLayout(corpusButtons);
    m_corpusStatus = new QLabel(this);
    m_corpusStatus->setWordWrap(true);
    layout->addWidget(m_corpusStatus);
    setManagedCorpusStatus(CorpusUpdateState::Hidden, {}, {}, 0);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(m_status);

    connect(reload, &QToolButton::clicked, this, &SongBrowser::refresh);
    connect(newSong, &QPushButton::clicked, this, &SongBrowser::newSongRequested);
    connect(folder, &QPushButton::clicked, this, &SongBrowser::changeFolderRequested);
    connect(m_updateCorpus, &QPushButton::clicked, this, &SongBrowser::updateCorpusRequested);
    connect(m_manageBackups, &QPushButton::clicked, this,
        &SongBrowser::manageBackupsRequested);
    connect(m_search, &QLineEdit::textChanged, this, &SongBrowser::repopulate);
    // A click opens; the arrow keys only move, so browsing with the keyboard
    // does not load two hundred songs on the way past.
    connect(m_tree, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem *item, int) { openItem(item); });
    connect(m_tree, &QTreeWidget::itemActivated, this,
        [this](QTreeWidgetItem *item, int) { openItem(item); });

    repopulate();
}

void SongBrowser::setManagedCorpusStatus(CorpusUpdateState state, const QString &summary,
    const QString &toolTip, int backupCount)
{
    const bool visible = state != CorpusUpdateState::Hidden;
    m_updateCorpus->setVisible(visible);
    m_manageBackups->setVisible(visible);
    m_corpusStatus->setVisible(visible);
    if (!visible)
        return;

    m_corpusStatus->setText(summary);
    m_corpusStatus->setToolTip(toolTip);
    m_updateCorpus->setToolTip(toolTip);
    m_manageBackups->setText(tr("Backups (%1)…").arg(backupCount));
    m_manageBackups->setEnabled(backupCount > 0);
    m_updateCorpus->setEnabled(state != CorpusUpdateState::Checking);
    m_updateCorpus->setStyleSheet({});
    m_corpusStatus->setStyleSheet(QStringLiteral("color: palette(mid);"));
    switch (state) {
    case CorpusUpdateState::Checking:
        m_updateCorpus->setText(tr("Checking OP-songs…"));
        break;
    case CorpusUpdateState::Current:
        m_updateCorpus->setText(tr("OP-songs is current"));
        break;
    case CorpusUpdateState::UpdateAvailable:
        m_updateCorpus->setText(tr("Update OP-songs…"));
        m_updateCorpus->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: #f0c84b; color: #202124; font-weight: 600; "
            "border: 1px solid #9a7a00; border-radius: 4px; padding: 5px; } "
            "QPushButton:hover { background-color: #ffda62; }"));
        m_corpusStatus->setStyleSheet(QStringLiteral("color: #8a6500; font-weight: 600;"));
        break;
    case CorpusUpdateState::CheckFailed:
        m_updateCorpus->setText(tr("Check for updates…"));
        break;
    case CorpusUpdateState::Hidden:
        break;
    }
}

void SongBrowser::refresh()
{
    const QString wanted = m_currentPath;
    m_library->rescan();
    repopulate();
    showCurrent(wanted);
}

void SongBrowser::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}

void SongBrowser::openItem(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (!path.isEmpty() && path != m_currentPath)
        Q_EMIT openRequested(path);
}

void SongBrowser::showCurrent(const QString &path)
{
    m_currentPath = path;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == path) {
            m_tree->setCurrentItem(item);
            m_tree->scrollToItem(item);
            return;
        }
    }
}

void SongBrowser::repopulate()
{
    m_tree->clear();
    if (m_library->root().isEmpty()) {
        m_status->setText(tr("No songs folder set yet. Choose <b>Folder…</b> and point it at "
                             "the directory holding 1/, 2/, 3/ …"));
        return;
    }

    const QList<SongEntry> matches = m_library->search(m_search->text());
    for (const SongEntry &entry : matches) {
        auto *item = new SongTreeItem(m_tree);
        item->setText(0, QString::number(entry.id));
        item->setText(1, entry.displayTitle());
        item->setText(2, entry.allLanguages().join(u' '));
        item->setData(0, Qt::UserRole, entry.basePath);
        QString tip = entry.displayTitle();
        if (!entry.subtitle.isEmpty())
            tip += QStringLiteral("\n%1").arg(entry.subtitle);
        tip += QStringLiteral("\n%1").arg(entry.basePath);
        if (!entry.problem.isEmpty()) {
            item->setForeground(1, QColor(0xd1, 0x24, 0x2f));
            tip += QStringLiteral("\n%1").arg(entry.problem);
        } else if (!entry.active) {
            item->setForeground(1, QColor(0x88, 0x88, 0x88));
            tip += QStringLiteral("\n%1").arg(tr("active = false; not published"));
        }
        for (int column = 0; column < 3; ++column)
            item->setToolTip(column, tip);
    }
    m_tree->resizeColumnToContents(0);
    m_tree->resizeColumnToContents(2);

    const int total = static_cast<int>(m_library->entries().size());
    m_status->setText(matches.size() == total ? tr("%n song(s)", nullptr, total)
                                              : tr("%1 of %2 songs match")
                                                    .arg(matches.size())
                                                    .arg(total));
    m_status->setToolTip(m_library->root());
    showCurrent(m_currentPath);
}

} // namespace ope::ui
