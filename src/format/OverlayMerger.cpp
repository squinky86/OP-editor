// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "OverlayMerger.hpp"

namespace OpenPsalm {

SongData OverlayMerger::merge(const SongData& baseSong, const SongData& overlaySong) {
    // Start with a copy of the base song
    SongData merged = baseSong;
    merged.isTranslationOverlay = true;

    // Apply overlay scalar overrides
    if (overlaySong.overridesTitle) merged.title = overlaySong.title;
    if (overlaySong.overridesSubtitle) merged.subtitle = overlaySong.subtitle;
    if (overlaySong.overridesActive) merged.active = overlaySong.active;
    if (overlaySong.overridesLanguage) merged.language = overlaySong.language;
    if (overlaySong.overridesVerseCount) merged.verseCount = overlaySong.verseCount;
    if (overlaySong.overridesKeySignature) merged.keySignature = overlaySong.keySignature;
    if (overlaySong.overridesTimeSig) {
        merged.timeSigNumerator = overlaySong.timeSigNumerator;
        merged.timeSigDenominator = overlaySong.timeSigDenominator;
    }
    if (overlaySong.overridesTempoBpm) merged.tempoBpm = overlaySong.tempoBpm;
    if (overlaySong.overridesCommentary) merged.commentary = overlaySong.commentary;

    // Array overrides: replace entire array if present in overlay
    if (overlaySong.overridesCopyrights) merged.copyrights = overlaySong.copyrights;
    if (overlaySong.overridesTimeSigChanges) merged.timeSigChanges = overlaySong.timeSigChanges;
    if (overlaySong.overridesPhraseBreaks) merged.phraseBreaks = overlaySong.phraseBreaks;
    if (overlaySong.overridesOptionalPhraseBreaks) merged.optionalPhraseBreaks = overlaySong.optionalPhraseBreaks;
    if (overlaySong.overridesNonBreakingPhraseBreaks) merged.nonBreakingPhraseBreaks = overlaySong.nonBreakingPhraseBreaks;

    // Wholesale replacement of global lyrics if defined in overlay
    if (overlaySong.overridesLyrics) {
        merged.lyrics = overlaySong.lyrics;
    }

    // Merge parts field-wise
    for (auto it = overlaySong.parts.begin(); it != overlaySong.parts.end(); ++it) {
        const QString& partName = it.key();
        const PartData& overlayPart = it.value();

        if (merged.parts.contains(partName)) {
            PartData& basePart = merged.parts[partName];
            if (overlayPart.overridesChoralType) {
                basePart.choralType = overlayPart.choralType;
                basePart.customChoralType = overlayPart.customChoralType;
            }
            if (overlayPart.overridesClef) basePart.clef = overlayPart.clef;
            if (overlayPart.overridesStaffNumber) basePart.staffNumber = overlayPart.staffNumber;
            if (overlayPart.overridesNotes) {
                basePart.notesText = overlayPart.notesText;
                basePart.parsedMeasures = overlayPart.parsedMeasures;
            }
            if (overlayPart.overridesSuppressVerses) basePart.suppressVerses = overlayPart.suppressVerses;
            if (overlayPart.overridesSuppressVersesWhen) basePart.suppressVersesWhen = overlayPart.suppressVersesWhen;
            if (overlayPart.overridesSpliceLyricsInto) basePart.spliceLyricsInto = overlayPart.spliceLyricsInto;
            if (overlayPart.overridesLyrics) {
                basePart.lyrics = overlayPart.lyrics;
            }
        } else {
            // New part defined in overlay
            merged.parts.insert(partName, overlayPart);
        }
    }

    return merged;
}

} // namespace OpenPsalm
