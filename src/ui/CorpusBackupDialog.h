// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTreeWidget;
QT_END_NAMESPACE

namespace ope::ui {

class CorpusBackupDialog : public QDialog {
    Q_OBJECT
public:
    explicit CorpusBackupDialog(QString target, QWidget *parent = nullptr);

    [[nodiscard]] QString selectedBackup() const { return m_selectedBackup; }

private:
    void refresh();
    void updateButtons();
    void deleteSelected();
    void restoreSelected();

    QString m_target;
    QString m_selectedBackup;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_note = nullptr;
    QPushButton *m_restore = nullptr;
    QPushButton *m_delete = nullptr;
};

} // namespace ope::ui
