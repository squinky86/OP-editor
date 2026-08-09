// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Validator.hpp"
#include "notation/LyricAligner.hpp"
#include <QSet>

namespace OpenPsalm {

namespace {

int getExpectedMeasureTicks(int measureNum, const SongData& song) {
    int num = song.timeSigNumerator;
    int den = song.timeSigDenominator;

    for (const auto& tsc : song.timeSigChanges) {
        if (measureNum >= tsc.measure) {
            if (tsc.duration <= 0 || measureNum < tsc.measure + tsc.duration) {
                num = tsc.numerator;
                den = tsc.denominator;
            }
        }
    }

    // Quarter is 48 ticks. Standard ticks = num * (48 * 4 / den)
    return num * (Ticks::Quarter * 4 / den);
}

} // anonymous namespace

std::vector<Diagnostic> Validator::validateSong(const SongData& song, const QString& filePath) {
    std::vector<Diagnostic> diagnostics;

    auto addDiag = [&](DiagnosticSeverity sev, const QString& code, const QString& msg, const QString& partName = QString(), int measure = 0) {
        Diagnostic d;
        d.severity = sev;
        d.code = code;
        d.message = msg;
        d.filePath = filePath;
        d.partName = partName;
        d.measure = measure;
        diagnostics.push_back(d);
    };

    // 1. Required metadata checks (for complete songs)
    if (!song.isTranslationOverlay) {
        if (song.title.trimmed().isEmpty()) {
            addDiag(DiagnosticSeverity::Error, QStringLiteral("MISSING_TITLE"), QStringLiteral("Song title is required."));
        }
        if (song.verseCount <= 0) {
            addDiag(DiagnosticSeverity::Error, QStringLiteral("INVALID_VERSE_COUNT"), QStringLiteral("Verse count must be positive."));
        }
        if (song.keySignature.trimmed().isEmpty()) {
            addDiag(DiagnosticSeverity::Error, QStringLiteral("MISSING_KEY"), QStringLiteral("Key signature is required."));
        }
        if (song.timeSigNumerator <= 0 || song.timeSigDenominator <= 0) {
            addDiag(DiagnosticSeverity::Error, QStringLiteral("INVALID_TIME_SIG"), QStringLiteral("Time signature numerator and denominator must be positive."));
        }
        if (song.tempoBpm <= 0) {
            addDiag(DiagnosticSeverity::Error, QStringLiteral("INVALID_TEMPO"), QStringLiteral("Tempo BPM must be positive."));
        }
        if (song.parts.isEmpty()) {
            addDiag(DiagnosticSeverity::Error, QStringLiteral("NO_PARTS"), QStringLiteral("Song must contain at least one part."));
        }
    }

    // 2. Measure counts and duration parity
    int expectedMeasureCount = -1;
    QString firstPartName;

    for (auto it = song.parts.begin(); it != song.parts.end(); ++it) {
        const QString& partName = it.key();
        const PartData& part = it.value();

        if (part.parsedMeasures.empty()) {
            if (!song.isTranslationOverlay || part.overridesNotes) {
                addDiag(DiagnosticSeverity::Error, QStringLiteral("EMPTY_PART_NOTES"), QStringLiteral("Part '%1' has no notes.").arg(partName), partName);
            }
            continue;
        }

        int mCount = static_cast<int>(part.parsedMeasures.size());
        if (expectedMeasureCount < 0) {
            expectedMeasureCount = mCount;
            firstPartName = partName;
        } else if (expectedMeasureCount != mCount) {
            addDiag(DiagnosticSeverity::Error, QStringLiteral("MEASURE_COUNT_MISMATCH"),
                    QStringLiteral("Part '%1' has %2 measures, but part '%3' has %4 measures.")
                    .arg(partName).arg(mCount).arg(firstPartName).arg(expectedMeasureCount),
                    partName);
        }

        // Measure-by-measure duration validation
        for (int mIdx = 0; mIdx < mCount; ++mIdx) {
            const Measure& m = part.parsedMeasures[mIdx];
            int expectedTicks = getExpectedMeasureTicks(m.index, song);
            int actualTicks = m.totalInternalTicks();

            // Allow pickup measures on measure 1 to be shorter than a full measure if padded with spacers or valid pickup
            if (m.index == 1 && actualTicks < expectedTicks && actualTicks > 0) {
                // Style warning if pickup is not padded
                addDiag(DiagnosticSeverity::Info, QStringLiteral("PICKUP_MEASURE"),
                        QStringLiteral("Measure 1 is a pickup measure (%1 / %2 ticks).").arg(actualTicks).arg(expectedTicks),
                        partName, m.index);
            } else if (actualTicks != expectedTicks) {
                addDiag(DiagnosticSeverity::Error, QStringLiteral("MEASURE_DURATION_MISMATCH"),
                        QStringLiteral("Measure %1 in part '%2' duration is %3 ticks, expected %4 ticks.")
                        .arg(m.index).arg(partName).arg(actualTicks).arg(expectedTicks),
                        partName, m.index);
            }

            // Check non-soprano tempo spanners (Style warning)
            if (part.choralType != ChoralType::Soprano) {
                for (const auto& ev : m.events) {
                    if (ev.tempoSpanner != TempoSpanner::None) {
                        addDiag(DiagnosticSeverity::Warning, QStringLiteral("NON_SOPRANO_TEMPO_SPANNER"),
                                QStringLiteral("Tempo spanners should be placed on Soprano only (found on %1).").arg(partName),
                                partName, m.index);
                        break;
                    }
                }
            }
        }
    }

    // 3. Phrase breaks validation
    int maxM = song.maxMeasureCount();
    auto validatePhraseBreaks = [&](const std::vector<PhraseBreak>& breaks, const QString& fieldName) {
        for (const auto& pb : breaks) {
            if (pb.measure < 1 || (maxM > 0 && pb.measure > maxM)) {
                addDiag(DiagnosticSeverity::Error, QStringLiteral("INVALID_PHRASE_BREAK_MEASURE"),
                        QStringLiteral("%1 refers to invalid measure %2 (song has %3 measures).").arg(fieldName).arg(pb.measure).arg(maxM));
            }
            int maxPhraseTicks = Ticks::internalToPhrase(getExpectedMeasureTicks(pb.measure, song));
            if (pb.phraseTicks < 0 || pb.phraseTicks >= maxPhraseTicks) {
                addDiag(DiagnosticSeverity::Error, QStringLiteral("INVALID_PHRASE_BREAK_TICK"),
                        QStringLiteral("%1 tick %2 is outside measure %3 bounds [0..%4).").arg(fieldName).arg(pb.phraseTicks).arg(pb.measure).arg(maxPhraseTicks));
            }
        }
    };

    validatePhraseBreaks(song.phraseBreaks, QStringLiteral("phrase_breaks"));
    validatePhraseBreaks(song.optionalPhraseBreaks, QStringLiteral("optional_phrase_breaks"));
    validatePhraseBreaks(song.nonBreakingPhraseBreaks, QStringLiteral("non_breaking_phrase_breaks"));

    // 4. Lyric alignment checks
    for (auto pit = song.parts.begin(); pit != song.parts.end(); ++pit) {
        const QString& partName = pit.key();
        const PartData& part = pit.value();
        if (part.parsedMeasures.empty()) continue;

        auto lyricSlots = LyricAligner::computeSlots(part.parsedMeasures);
        int totalSoundingSlots = 0;
        for (const auto& slot : lyricSlots) {
            if (!slot.isSlurredMelisma) {
                totalSoundingSlots++;
            }
        }

        // Check global verses
        for (int v = 1; v <= song.verseCount; ++v) {
            QString vKey = QString::number(v);
            QString lyricsText = part.lyrics.value(vKey, song.lyrics.section(vKey));
            if (!lyricsText.isEmpty()) {
                QStringList syllables = LyricAligner::parseSyllables(lyricsText);
                if (syllables.size() != totalSoundingSlots && totalSoundingSlots > 0) {
                    addDiag(DiagnosticSeverity::Warning, QStringLiteral("LYRIC_SLOT_MISMATCH"),
                            QStringLiteral("Part '%1' verse %2 has %3 syllables for %4 note slots.")
                            .arg(partName).arg(v).arg(syllables.size()).arg(totalSoundingSlots),
                            partName);
                }
            }
        }
    }

    // 5. Paired suppression check (Style warning)
    for (auto pit = song.parts.begin(); pit != song.parts.end(); ++pit) {
        const QString& partName = pit.key();
        const PartData& part = pit.value();
        if (!part.suppressVerses.empty() && part.suppressVersesWhen.isEmpty()) {
            addDiag(DiagnosticSeverity::Warning, QStringLiteral("UNPAIRED_SUPPRESSION"),
                    QStringLiteral("Part '%1' defines suppress_verses without suppress_verses_when.").arg(partName),
                    partName);
        }
    }

    return diagnostics;
}

} // namespace OpenPsalm
