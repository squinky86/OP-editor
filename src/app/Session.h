// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The open song: its base file, its translations, the current selection, the
// undo stack, and the findings. Every widget reads from here and mutates
// through here, so there is exactly one copy of the truth on screen.

#pragma once

#include "core/Library.h"
#include "core/Lyrics.h"
#include "core/Playback.h"
#include "core/Song.h"
#include "core/Validator.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QUndoGroup>
#include <QUndoStack>

namespace ope {

/// What the score cursor points at.
struct Selection {
    int partIndex = -1;
    int measureIndex = -1;
    int eventIndex = -1;

    [[nodiscard]] bool isValid() const noexcept { return partIndex >= 0 && measureIndex >= 0; }
    [[nodiscard]] bool hasEvent() const noexcept { return isValid() && eventIndex >= 0; }
    [[nodiscard]] bool operator==(const Selection &) const = default;
};

class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(QObject *parent = nullptr);

    /// Open a song directory's base file and every translation beside it.
    /// A TOML syntax error is fatal: nothing is opened and the error is returned.
    [[nodiscard]] std::expected<void, LoadError> openSong(const QString &basePath);
    /// Adopt an in-memory document (the new-song wizard's output). `path` is
    /// where it will be written.
    void adoptNewDocument(SongDocument document);
    /// Add an in-memory translation without touching disk. It follows the same
    /// save, undo, and discard rules as an existing language document.
    [[nodiscard]] bool adoptNewOverlay(SongDocument document);
    void close();

    [[nodiscard]] bool isOpen() const noexcept { return !m_languages.isEmpty(); }
    [[nodiscard]] const QStringList &languages() const noexcept { return m_languages; }
    [[nodiscard]] const QString &currentLanguage() const noexcept { return m_currentLanguage; }
    void setCurrentLanguage(const QString &code);

    [[nodiscard]] SongDocument &document();
    [[nodiscard]] const SongDocument &document() const;
    [[nodiscard]] const SongDocument *document(const QString &language) const;
    /// The document as the seeder sees it: an overlay merged onto its base.
    [[nodiscard]] const SongDocument &effectiveDocument() const;
    [[nodiscard]] const SongDocument *baseDocument() const;

    [[nodiscard]] QUndoStack *undoStack() noexcept;
    [[nodiscard]] QUndoGroup *undoGroup() noexcept { return &m_undoGroup; }
    [[nodiscard]] const QList<Finding> &findings() const noexcept { return m_findings; }
    [[nodiscard]] QList<Finding> findings(const QString &language) const;
    [[nodiscard]] const PartAlignment &alignment(const QString &partName) const;

    [[nodiscard]] Selection selection() const noexcept { return m_selection; }
    void setSelection(Selection selection);
    [[nodiscard]] const Event *selectedEvent() const;

    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool isDirty(const QString &language) const;
    [[nodiscard]] QStringList dirtyLanguages() const;
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] bool isNewFile() const noexcept { return isNewFile(m_currentLanguage); }
    [[nodiscard]] bool isNewFile(const QString &language) const noexcept;

    struct SaveError {
        enum class Kind { Conflict, Io, Reload };
        Kind kind = Kind::Io;
        QString path;
        QString message;
    };

    /// Save one authored file. Unless `overwriteExternalChanges` is true, the
    /// exact disk bytes must still match what was opened.
    [[nodiscard]] std::expected<void, SaveError> save(
        const QString &language, bool overwriteExternalChanges = false);
    [[nodiscard]] std::expected<void, SaveError> save(bool overwriteExternalChanges = false)
    {
        return save(m_currentLanguage, overwriteExternalChanges);
    }

    /// Apply a mutation as one undo step. `mutate` receives the live document;
    /// the session snapshots around it, re-parses, and revalidates.
    void mutate(const QString &description, const std::function<void(SongDocument &)> &mutate);
    /// Apply a mutation to a specific document. This lets a delayed editor
    /// commit safely to the document where typing began even after a tab switch.
    void mutate(const QString &language, const QString &description,
        const std::function<void(SongDocument &)> &mutate);

    /// Replace the document wholesale (used by undo/redo).
    void restore(const QString &language, const SongDocument &document);

    [[nodiscard]] PlaybackPlan buildPlaybackPlan(const PlaybackOptions &options) const;

    // -- phrase breaks
    //
    // Three fields hold them and a break belongs to exactly one, so adding,
    // removing, and moving between lanes all go through here rather than being
    // reimplemented by every view that lets the user place one.

    /// Which lane holds a break at this position, if any.
    [[nodiscard]] std::optional<BreakKind> phraseBreakAt(PhraseBreak position) const;
    /// Put `position` in `kind`, removing it from whichever lane holds it now.
    void setPhraseBreak(PhraseBreak position, std::optional<BreakKind> kind);
    /// `kind` if the position is empty or in another lane, nothing if it is
    /// already there — a click that adds, then takes away.
    void togglePhraseBreak(PhraseBreak position, BreakKind kind);

Q_SIGNALS:
    void documentChanged();
    void selectionChanged();
    void languageChanged();
    void dirtyChanged();

private:
    [[nodiscard]] SongDocument effectiveDocument(const QString &language) const;
    QUndoStack *ensureUndoStack(const QString &language);
    void refresh();

    QHash<QString, SongDocument> m_documents;  ///< keyed by language code
    QHash<QString, PartAlignment> m_alignments;
    mutable SongDocument m_effective;
    QStringList m_languages;
    QString m_currentLanguage;
    QString m_baseLanguage;
    QList<Finding> m_findings;
    Selection m_selection;
    QHash<QString, QUndoStack *> m_undoStacks;
    QUndoGroup m_undoGroup;
    QSet<QString> m_newFiles;
    PartAlignment m_emptyAlignment;
};

} // namespace ope
