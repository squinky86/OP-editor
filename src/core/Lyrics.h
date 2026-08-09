// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Lyric slot bookkeeping: which notes take a syllable, where each verse starts,
// and how `@sN` shared sections expand.
//
// A port of `attach_lyrics` in OpenPsalm's src/seed/importer.rs. The alignment
// grid in the UI is only trustworthy if it computes lyricSlots the same way the
// seeder does, down to the rule that a verse whose text is empty is not a verse.

#pragma once

#include "Song.h"

#include <QList>
#include <QMap>
#include <QString>

namespace ope {

/// One lyric slot of one part: the event that carries it and its position.
struct Slot {
    int measureIndex = 0;
    int eventIndex = 0;
    int tick = 0;            ///< cumulative internal ticks from the song start
    int tickInMeasure = 0;
    bool dashedContinuation = false;  ///< inside a dashed slur: keeps its own slot
};

/// A lyric key resolved for one part: its syllables and where they attach.
struct AttachedSection {
    QString key;             ///< "1", "chorus", "coda", …
    bool isChorus = false;
    bool isCoda = false;
    int verseNumber = 1;
    QStringList syllables;   ///< after @sN expansion
    int slotOffset = 0;      ///< index of the first slot this section attaches to
    bool textInherited = false;  ///< came from the global map, not the part's own
};

/// Everything the UI and the validator need about one part's lyrics.
struct PartAlignment {
    QString partName;
    QList<Slot> lyricSlots;
    QList<AttachedSection> sections;
    int maxVerseLength = 0;
    int maxChorusLength = 0;
    bool hasVerseLyrics = false;
    bool chorusFirst = false;
    std::optional<int> chorusStartSlot;
    std::optional<int> codaStartSlot;
    QStringList errors;  ///< shared-section failures, reported as E-SHARED-*

    /// Syllable for a slot in a section, or an empty string when out of range.
    [[nodiscard]] QString syllableAt(const AttachedSection &section, int slot) const;
};

/// Build the per-part slot list and resolve every lyric key for it.
[[nodiscard]] PartAlignment alignPart(const SongDocument &doc, const Part &part);

/// Expand `@sN` references against a merged lyric map. Shared entries are
/// removed: they are templates, not lyric rows.
[[nodiscard]] QMap<QString, QString> expandSharedLyrics(
    const QMap<QString, QString> &merged, QStringList *errors);

/// Merge global and per-part lyric maps the way the importer does: a part key
/// overrides the global key of the same name, and global keys the part does not
/// redefine are kept.
[[nodiscard]] QMap<QString, QString> mergedLyricTexts(
    const SongDocument &doc, const Part &part, QSet<QString> *inheritedKeys = nullptr);

/// Per-verse outcome of an echo splice (`splice_lyrics_into`).
struct SpliceReport {
    bool configured = false;
    QString targetChoralType;
    QString targetPartName;
    struct VerseResult {
        QString key;
        bool splices = false;
        QString reason;  ///< why it does not, when it does not
    };
    QList<VerseResult> verses;
};

/// Decide, per verse, whether this part's syllables form a pure tail of the
/// target voice's — the condition the print exporters require.
[[nodiscard]] SpliceReport analyseSplice(const SongDocument &doc, const Part &part);

} // namespace ope
