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
    void close();

    [[nodiscard]] bool isOpen() const noexcept { return !m_languages.isEmpty(); }
    [[nodiscard]] const QStringList &languages() const noexcept { return m_languages; }
    [[nodiscard]] const QString &currentLanguage() const noexcept { return m_currentLanguage; }
    void setCurrentLanguage(const QString &code);

    [[nodiscard]] SongDocument &document();
    [[nodiscard]] const SongDocument &document() const;
    /// The document as the seeder sees it: an overlay merged onto its base.
    [[nodiscard]] const SongDocument &effectiveDocument() const;
    [[nodiscard]] const SongDocument *baseDocument() const;

    [[nodiscard]] QUndoStack *undoStack() noexcept { return &m_undo; }
    [[nodiscard]] const QList<Finding> &findings() const noexcept { return m_findings; }
    [[nodiscard]] const PartAlignment &alignment(const QString &partName) const;

    [[nodiscard]] Selection selection() const noexcept { return m_selection; }
    void setSelection(Selection selection);
    [[nodiscard]] const Event *selectedEvent() const;

    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] bool isNewFile() const noexcept { return m_isNewFile; }
    [[nodiscard]] std::expected<void, QString> save();

    /// Apply a mutation as one undo step. `mutate` receives the live document;
    /// the session snapshots around it, re-parses, and revalidates.
    void mutate(const QString &description, const std::function<void(SongDocument &)> &mutate);

    /// Replace the document wholesale (used by undo/redo).
    void restore(const QString &language, const SongDocument &document);

    [[nodiscard]] PlaybackPlan buildPlaybackPlan(const PlaybackOptions &options) const;

Q_SIGNALS:
    void documentChanged();
    void selectionChanged();
    void languageChanged();
    void dirtyChanged();

private:
    void refresh();

    QHash<QString, SongDocument> m_documents;  ///< keyed by language code
    QHash<QString, PartAlignment> m_alignments;
    mutable SongDocument m_effective;
    QStringList m_languages;
    QString m_currentLanguage;
    QString m_baseLanguage;
    QList<Finding> m_findings;
    Selection m_selection;
    QUndoStack m_undo;
    bool m_isNewFile = false;
    PartAlignment m_emptyAlignment;
};

} // namespace ope
