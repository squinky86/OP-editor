// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "CorpusBackupDialog.h"

#include "core/CorpusSnapshot.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace ope::ui {
namespace {

QString displayDate(const QDateTime &date)
{
    return date.isValid()
        ? QLocale::system().toString(date.toLocalTime(), QLocale::ShortFormat)
        : QObject::tr("Unknown");
}

} // namespace

CorpusBackupDialog::CorpusBackupDialog(QString target, QWidget *parent)
    : QDialog(parent)
    , m_target(std::move(target))
{
    setWindowTitle(tr("Managed OP-songs Backups"));
    resize(780, 360);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(
        tr("Each successful replacement retains the previous validated corpus. Restoring "
           "checks the selected backup again and retains the current corpus as another "
           "backup."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels(
        { tr("Backup"), tr("Commit"), tr("Commit date"), tr("Current as of") });
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; ++column)
        m_tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(m_note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_restore = buttons->addButton(tr("Restore selected…"), QDialogButtonBox::AcceptRole);
    m_delete = buttons->addButton(
        tr("Delete selected permanently…"), QDialogButtonBox::DestructiveRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
        &CorpusBackupDialog::updateButtons);
    connect(m_restore, &QPushButton::clicked, this, &CorpusBackupDialog::restoreSelected);
    connect(m_delete, &QPushButton::clicked, this, &CorpusBackupDialog::deleteSelected);
    refresh();
}

void CorpusBackupDialog::refresh()
{
    m_tree->clear();
    const QStringList backups = corpus::backupDirectories(m_target);
    for (const QString &path : backups) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QFileInfo(path).fileName());
        item->setData(0, Qt::UserRole, path);
        const auto snapshot = corpus::readSnapshot(path);
        if (snapshot) {
            item->setText(1, snapshot->commitSha.isEmpty()
                    ? tr("Legacy snapshot")
                    : corpus::abbreviatedSha(snapshot->commitSha));
            item->setText(2, displayDate(snapshot->commitDate));
            item->setText(3, displayDate(snapshot->currentAsOf));
            item->setToolTip(0, path);
        } else {
            item->setText(1, tr("Metadata unavailable"));
            item->setText(2, displayDate(QFileInfo(path).lastModified()));
            item->setToolTip(0, tr("%1\n%2").arg(path, snapshot.error()));
        }
    }
    m_note->setText(backups.isEmpty()
            ? tr("No managed-corpus backups are present.")
            : tr("Deleting a backup is permanent. Restore is safer when you are unsure."));
    updateButtons();
}

void CorpusBackupDialog::updateButtons()
{
    const bool selected = m_tree->currentItem() != nullptr;
    m_restore->setEnabled(selected);
    m_delete->setEnabled(selected);
}

void CorpusBackupDialog::deleteSelected()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;
    const QString path = item->data(0, Qt::UserRole).toString();
    QMessageBox confirmation(QMessageBox::Warning, tr("Delete backup permanently?"),
        tr("This permanently removes the managed-corpus backup:\n\n%1\n\nThis action "
           "cannot be undone.")
            .arg(path),
        QMessageBox::NoButton, this);
    auto *deleteButton
        = confirmation.addButton(tr("Delete permanently"), QMessageBox::DestructiveRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != deleteButton) {
        return;
    }
    if (!corpus::backupDirectories(m_target).contains(QFileInfo(path).absoluteFilePath())) {
        QMessageBox::critical(this, tr("Unsafe backup path"),
            tr("The selected path is no longer a managed OP-songs backup:\n\n%1")
                .arg(path));
        refresh();
        return;
    }
    if (!QDir(path).removeRecursively()) {
        QMessageBox::critical(
            this, tr("Could not delete backup"), tr("Could not completely remove %1.").arg(path));
        return;
    }
    refresh();
}

void CorpusBackupDialog::restoreSelected()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;
    m_selectedBackup = item->data(0, Qt::UserRole).toString();
    accept();
}

} // namespace ope::ui
