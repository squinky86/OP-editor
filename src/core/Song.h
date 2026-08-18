// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The song document: every field the TOML format defines, each remembering the
// byte span it came from and whether the user has touched it.
//
// Saving replaces only the spans of dirty fields, so a file round-trips byte for
// byte when nothing changed and produces a one-line diff when one thing did.

#pragma once

#include "Notation.h"
#include "Toml.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

namespace ope {

/// A value that may be absent, knows where it was written, and tracks edits.
template <typename T>
class Field {
public:
    [[nodiscard]] bool present() const noexcept { return m_value.has_value(); }
    [[nodiscard]] bool dirty() const noexcept { return m_dirty; }
    [[nodiscard]] toml::Span span() const noexcept { return m_span; }
    [[nodiscard]] const std::optional<T> &opt() const noexcept { return m_value; }

    [[nodiscard]] T valueOr(T fallback) const { return m_value.value_or(std::move(fallback)); }
    [[nodiscard]] const T &operator*() const { return *m_value; }
    [[nodiscard]] const T *operator->() const { return &*m_value; }

    /// Bind a value read from the file: present, and not an edit.
    void bind(T value, toml::Span span)
    {
        m_value = std::move(value);
        m_span = span;
        m_dirty = false;
    }
    void set(T value)
    {
        if (m_value && *m_value == value)
            return;
        m_value = std::move(value);
        m_dirty = true;
    }
    void clear()
    {
        if (!m_value)
            return;
        m_value.reset();
        m_dirty = true;
    }
    void markClean() { m_dirty = false; }

private:
    std::optional<T> m_value;
    toml::Span m_span;
    bool m_dirty = false;
};

struct TimeSigChange {
    int measure = 1;
    int numerator = 4;
    int denominator = 4;
    int duration = 1;

    [[nodiscard]] bool operator==(const TimeSigChange &) const = default;
};

/// A `"M:T"` phrase-break entry. `tick` counts 64ths, as the format does.
struct PhraseBreak {
    int measure = 1;
    int tick = 0;

    [[nodiscard]] QString toString() const;
    [[nodiscard]] bool operator==(const PhraseBreak &) const = default;
    [[nodiscard]] bool operator<(const PhraseBreak &other) const
    {
        return measure != other.measure ? measure < other.measure : tick < other.tick;
    }
    static std::optional<PhraseBreak> fromString(QStringView text);
};

enum class BreakKind { Required, Optional, NonBreaking };

struct LyricSection {
    QString rawText;
    toml::Span span;       ///< the `text = "…"` value
    toml::Span tableSpan;  ///< the whole `[lyrics.N]` block, for deletion
    bool dirty = false;

    /// Whitespace-delimited syllables, with ` -- ` connectors removed.
    [[nodiscard]] QStringList syllables() const;
    /// Syllable position within its word: begin / middle / end / single.
    [[nodiscard]] QStringList syllableTypes() const;
};

struct Part {
    QString name;
    Field<QString> choralType;
    Field<QString> clef;
    Field<int> staffNumber;
    Field<QString> notes;
    Field<QList<int>> suppressVerses;
    Field<QStringList> suppressVersesWhen;
    Field<QString> spliceLyricsInto;
    QMap<QString, LyricSection> lyrics;  ///< per-part overrides

    NoteStream stream;
    QList<TokenIssue> tokenIssues;
    bool isNew = false;         ///< not present in the source file
    bool inheritedFromBase = false;  ///< overlay: this part came from song.toml
    bool notesInherited = false;     ///< overlay: notation not overridden here
    toml::Span tableSpan;
    QStringList tablePath;

    [[nodiscard]] QString displayChoralType() const { return choralType.valueOr(QString()); }
    /// SATB rank used for display order, matching the seeder's part sort.
    [[nodiscard]] int sortRank() const;
    void reparse();
};

/// One authored file: a base `song.toml` or a `song_{lang}.toml` overlay.
class SongDocument {
public:
    // -- identity
    QString path;
    int workId = 0;             ///< the songs/N directory number
    QString language;           ///< the language this file is authored in
    bool isOverlay = false;

    // -- header
    Field<QString> title;
    Field<QString> subtitle;
    Field<bool> active;
    Field<QString> declaredLanguage;
    Field<QString> keySignature;
    Field<int> timeSigNumerator;
    Field<int> timeSigDenominator;
    Field<int> tempoBpm;
    Field<int> verseCount;
    Field<QStringList> copyrights;
    Field<QString> commentary;
    Field<bool> convergeVerses;
    Field<QList<PhraseBreak>> phraseBreaks;
    Field<QList<PhraseBreak>> optionalPhraseBreaks;
    Field<QList<PhraseBreak>> nonBreakingPhraseBreaks;
    Field<QList<TimeSigChange>> timeSigChanges;

    QList<Part> parts;
    QMap<QString, LyricSection> lyrics;  ///< global; keys "1".."N", "chorus", "coda", "sN"
    /// Whole tables the user deleted (a per-part lyric override reverted to the
    /// song's default, say). Erased from the file on save; kept as spans because
    /// the section itself is gone from the map by then.
    QList<toml::Span> removedTables;

    // -- source
    toml::Document source;
    QByteArray originalBytes;
    /// True for the result of mergeOverlay(): a read-only view whose inherited
    /// parts carry spans into the *base* file. Never serialize one.
    bool isMergedView = false;
    QStringList unknownKeys;  ///< preserved verbatim, reported at info level

    // -- accessors
    [[nodiscard]] const Part *part(QStringView name) const;
    [[nodiscard]] Part *part(QStringView name);
    [[nodiscard]] QList<const Part *> partsInDisplayOrder() const;
    /// Effective time signature for a 1-based measure, honouring time_sig_changes.
    [[nodiscard]] std::pair<int, int> timeSigForMeasure(int measureNumber) const;
    [[nodiscard]] int expectedTicksForMeasure(int measureNumber) const;
    [[nodiscard]] int measureCount() const;
    [[nodiscard]] QList<PhraseBreak> allPhraseBreaks() const;
    [[nodiscard]] QStringList verseKeys() const;   ///< numbered verses, sorted
    [[nodiscard]] QStringList sharedKeys() const;  ///< "sN" sections, sorted
    [[nodiscard]] bool usesSharedLyrics() const;
    [[nodiscard]] bool effectiveConvergeVerses() const;

    [[nodiscard]] bool isDirty() const;
    void markClean();

    /// Drop a per-part lyric override, so the part falls back to the song's
    /// default text for that key. The `[parts.X.lyrics.KEY]` block goes with it.
    void removePartLyric(Part &part, const QString &key);
    /// Drop a whole `[lyrics.KEY]` section.
    void removeGlobalLyric(const QString &key);
    /// Add or replace one lyric entry, marking it for writing.
    static void setLyric(QMap<QString, LyricSection> &map, const QString &key,
        const QString &text);

    /// Lyric keys in the order they are written and displayed: numbered verses
    /// in numeric order, then chorus, coda, and the shared `sN` templates.
    [[nodiscard]] static QStringList orderedLyricKeys(const QStringList &keys);

    static bool isChorusKey(QStringView key);
    static bool isCodaKey(QStringView key);
    static bool isSharedKey(QStringView key);
    static bool isVerseKey(QStringView key);
};

struct LoadError {
    QString path;
    toml::ParseError parse;
    QString message;  ///< set for non-parse failures (missing file, unreadable)

    [[nodiscard]] QString formatted() const;
};

namespace io {

/// Read and parse one file. A syntax error is returned, never partially applied:
/// the caller refuses to open the document.
[[nodiscard]] std::expected<SongDocument, LoadError> load(const QString &path);

/// Parse authored bytes as `path` without touching the filesystem. The editable
/// Source panel uses this so a valid source draft can become the session's exact
/// document while disk-conflict checks continue to use their separate baseline.
[[nodiscard]] std::expected<SongDocument, LoadError> loadBytes(
    const QString &path, QByteArray bytes);

/// Serialize a document by splicing the spans of its dirty fields into the
/// original bytes. With nothing dirty the result is the original file.
[[nodiscard]] QByteArray serialize(const SongDocument &doc);

/// Emit a complete file from scratch, in the canonical field order used for new
/// songs and new translations.
[[nodiscard]] QByteArray serializeFresh(const SongDocument &doc);

/// Write `bytes` to `path` atomically (temp file in the same directory + rename).
[[nodiscard]] std::expected<void, QString> writeAtomically(
    const QString &path, const QByteArray &bytes);

} // namespace io

/// Copy an inherited lyric map into an overlay before its first edit.
///
/// In a translation, defining *any* entry of a lyric map replaces the whole map
/// — the merge is wholesale, not per key. Editing one verse of a `song_es.toml`
/// that has none of its own would therefore delete the rest. Every path that
/// writes a lyric into an overlay must call this first. `merged` is the view the
/// user is looking at; `partName` empty means the song-wide map. Does nothing to
/// a base document, or to a map the overlay already defines.
void materialiseOverlayLyrics(
    SongDocument &overlay, const SongDocument &merged, const QString &partName);

/// Merge an overlay onto a base, reproducing `SongData::merge_overlay`:
/// scalars and arrays replace, parts merge field-wise, lyric maps replace whole.
[[nodiscard]] SongDocument mergeOverlay(const SongDocument &base, const SongDocument &overlay);

} // namespace ope
