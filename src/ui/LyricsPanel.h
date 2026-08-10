// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Lyrics, two ways.
//
// The text view is organised by *section*, not by part, because that is where
// the format's one genuinely confusing feature lives: `[lyrics.1]` is what every
// voice sings unless a `[parts.Alto.lyrics.1]` exists, in which case the alto
// sings that instead. A list of identical-looking boxes cannot say which is
// which, so each section is one card: the song's own text at the top, every
// voice that overrides it underneath, each labelled with the voice it belongs
// to, each with its own syllable count against that voice's slots, and each
// removable in one click.
//
// The alignment grid is for proving a stanza fits: columns are lyric slots with
// the note above them, rows are verses, and a melisma slot is greyed out because
// it takes no syllable.

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
class QVBoxLayout;
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
    /// One editable text box: a song-wide section, or one voice's override of it.
    struct EditorRef {
        QString key;
        QString partName;  ///< empty for the song-wide default
        QPlainTextEdit *editor = nullptr;
        QLabel *counter = nullptr;
    };

    void rebuildSections();
    /// Which cards exist and which voices override each. Rebuilding the page is
    /// only allowed to happen when this changes: a card is torn down and rebuilt
    /// on every keystroke otherwise, and the caret goes with it.
    [[nodiscard]] QStringList structureSignature() const;
    void syncEditors();
    void buildSectionCard(QVBoxLayout *host, const QString &key);
    QWidget *buildTextRow(const QString &key, const QString &partName, const QString &text,
        const QStringList &appliesTo);
    void updateCounter(const EditorRef &row);

    void rebuildGrid();
    void commitCell(int row, int column);
    void commitText(const QString &key, const QString &partName, const QString &text);
    void addOverride(const QString &key, const QString &partName);
    void removeOverride(const QString &key, const QString &partName);
    void addSection(const QString &key);
    void insertUndertie();
    [[nodiscard]] QString gridPartName() const;
    /// Parts that sing `key` from the song-wide text, in display order.
    [[nodiscard]] QStringList partsUsingDefault(const QString &key) const;
    [[nodiscard]] QStringList partsOverriding(const QString &key) const;

    Session *m_session = nullptr;
    bool m_loading = false;

    QTabWidget *m_tabs = nullptr;
    QWidget *m_sectionHost = nullptr;
    QToolButton *m_undertie = nullptr;
    QToolButton *m_addSection = nullptr;
    QLabel *m_legend = nullptr;

    QComboBox *m_gridPart = nullptr;
    QTableWidget *m_grid = nullptr;
    QLabel *m_gridHint = nullptr;

    QList<EditorRef> m_editors;
    /// Typing is debounced into one undo step per pause, not one per keystroke.
    QHash<QString, QString> m_pendingCommits;
    QTimer m_commitTimer;
    QStringList m_signature;
};

} // namespace ope::ui
