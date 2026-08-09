// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Lyrics, two ways.
//
// The text view is for typing a stanza; the alignment grid is for proving it
// fits. Columns are lyric slots with the note above them, rows are verses, and
// a melisma slot is greyed out because it takes no syllable — which is the one
// thing about this format that is hard to see in a text editor and easy to see
// here.

#pragma once

#include "app/Session.h"

#include <QHash>
#include <QTimer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;
class QToolButton;
QT_END_NAMESPACE

namespace ope::ui {

class LyricsPanel : public QWidget {
    Q_OBJECT
public:
    explicit LyricsPanel(Session *session, QWidget *parent = nullptr);

    void refresh();
    /// Put the cursor on a slot of a part (the score's double-click target).
    void focusSlot(const QString &partName, int slot);

Q_SIGNALS:
    void statusMessage(const QString &message);

private:
    void rebuildTextEditors();
    void rebuildGrid();
    void commitText(const QString &key, const QString &text);
    void commitCell(int row, int column);
    [[nodiscard]] QString currentPartName() const;
    void insertUndertie();

    Session *m_session = nullptr;
    bool m_loading = false;
    QComboBox *m_partSelector = nullptr;
    QTabWidget *m_tabs = nullptr;
    QWidget *m_textPage = nullptr;
    QWidget *m_gridPage = nullptr;
    QTableWidget *m_grid = nullptr;
    QLabel *m_gridHint = nullptr;
    QToolButton *m_undertie = nullptr;
    QList<QPair<QString, QPlainTextEdit *>> m_editors;
    QWidget *m_editorHost = nullptr;
    /// Typing is debounced into one undo step per pause, not one per keystroke.
    QHash<QString, QString> m_pendingCommits;
    QTimer m_commitTimer;
};

} // namespace ope::ui
