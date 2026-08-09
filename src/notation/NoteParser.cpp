// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "NoteParser.hpp"
#include <cmath>

namespace OpenPsalm {

namespace {

int largestPowerOfTwoLessThan(int n) {
    int p = 1;
    while (p * 2 < n) {
        p *= 2;
    }
    return p;
}

} // anonymous namespace

ParseResult NoteParser::parse(const QString& notesText, const QString& partName, const QString& filePath) {
    ParseResult result;
    if (notesText.trimmed().isEmpty()) {
        return result;
    }

    int currentMeasureIndex = 1;
    Measure currentMeasure;
    currentMeasure.index = currentMeasureIndex;

    int tupletN = 0;
    int tupletM = 0;
    bool inTuplet = false;

    // Pending modifiers
    SectionMarker pendingMarker = SectionMarker::None;
    int pendingSharedSection = 0;
    bool pendingSlurStart = false;
    bool pendingBeamStart = false;
    TempoSpanner pendingTempoSpanner = TempoSpanner::None;
    Hairpin pendingHairpin = Hairpin::None;

    int i = 0;
    const int len = notesText.length();

    while (i < len) {
        QChar c = notesText[i];

        // Skip whitespace
        if (c.isSpace()) {
            i++;
            continue;
        }

        // Measure separator |
        if (c == QLatin1Char('|')) {
            if (inTuplet) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.code = QStringLiteral("TUPLET_BARLINE");
                diag.message = QStringLiteral("Tuplet cannot cross a barline in measure %1").arg(currentMeasureIndex);
                diag.partName = partName;
                diag.filePath = filePath;
                diag.measure = currentMeasureIndex;
                result.diagnostics.push_back(diag);
                result.hasErrors = true;
            }

            result.measures.push_back(currentMeasure);
            currentMeasureIndex++;
            currentMeasure = Measure();
            currentMeasure.index = currentMeasureIndex;
            i++;
            continue;
        }

        // Tuplet start: {N ... or {N:M ...
        if (c == QLatin1Char('{')) {
            if (inTuplet) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.code = QStringLiteral("NESTED_TUPLET");
                diag.message = QStringLiteral("Nested tuplets are not supported in measure %1").arg(currentMeasureIndex);
                diag.partName = partName;
                diag.filePath = filePath;
                diag.measure = currentMeasureIndex;
                result.diagnostics.push_back(diag);
                result.hasErrors = true;
            }

            int closeIdx = notesText.indexOf(QLatin1Char('}'), i + 1);
            if (closeIdx < 0) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.code = QStringLiteral("UNCLOSED_TUPLET");
                diag.message = QStringLiteral("Unclosed tuplet starting in measure %1").arg(currentMeasureIndex);
                diag.partName = partName;
                diag.filePath = filePath;
                diag.measure = currentMeasureIndex;
                result.diagnostics.push_back(diag);
                result.hasErrors = true;
            }

            // Parse ratio e.g. {3:2 or {3
            int headerEnd = i + 1;
            while (headerEnd < len && !notesText[headerEnd].isSpace() && notesText[headerEnd] != QLatin1Char('}')) {
                headerEnd++;
            }
            QString ratioStr = notesText.mid(i + 1, headerEnd - (i + 1));
            int colon = ratioStr.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                tupletN = ratioStr.left(colon).toInt();
                tupletM = ratioStr.mid(colon + 1).toInt();
            } else {
                tupletN = ratioStr.toInt();
                tupletM = largestPowerOfTwoLessThan(tupletN);
            }

            if (tupletN <= 0 || tupletM <= 0) {
                tupletN = 3;
                tupletM = 2;
            }

            inTuplet = true;
            i = headerEnd;
            continue;
        }

        // Tuplet end: }
        if (c == QLatin1Char('}')) {
            inTuplet = false;
            tupletN = 0;
            tupletM = 0;
            i++;
            continue;
        }

        // Chord: <...>duration...
        if (c == QLatin1Char('<')) {
            int chordClose = notesText.indexOf(QLatin1Char('>'), i + 1);
            if (chordClose < 0) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.code = QStringLiteral("UNCLOSED_CHORD");
                diag.message = QStringLiteral("Unclosed chord in measure %1").arg(currentMeasureIndex);
                diag.partName = partName;
                diag.filePath = filePath;
                diag.measure = currentMeasureIndex;
                result.diagnostics.push_back(diag);
                result.hasErrors = true;
                i++;
                continue;
            }

            // Find end of trailing token (duration + flags)
            int tokenEnd = chordClose + 1;
            while (tokenEnd < len && !notesText[tokenEnd].isSpace() && notesText[tokenEnd] != QLatin1Char('|') && notesText[tokenEnd] != QLatin1Char('}')) {
                tokenEnd++;
            }

            QString chordTokenStr = notesText.mid(i, tokenEnd - i);
            auto tok = NoteToken::fromString(chordTokenStr);
            if (!tok.has_value()) {
                Diagnostic diag;
                diag.severity = DiagnosticSeverity::Error;
                diag.code = QStringLiteral("INVALID_CHORD_TOKEN");
                diag.message = QStringLiteral("Invalid chord token '%1' in measure %2").arg(chordTokenStr).arg(currentMeasureIndex);
                diag.partName = partName;
                diag.filePath = filePath;
                diag.measure = currentMeasureIndex;
                result.diagnostics.push_back(diag);
                result.hasErrors = true;
            } else {
                NoteToken token = tok.value();
                if (inTuplet) {
                    token.duration.tupletN = tupletN;
                    token.duration.tupletM = tupletM;
                }
                if (pendingMarker != SectionMarker::None) {
                    token.sectionMarker = pendingMarker;
                    pendingMarker = SectionMarker::None;
                }
                if (pendingSharedSection > 0) {
                    token.sharedSectionIndex = pendingSharedSection;
                    pendingSharedSection = 0;
                }
                if (pendingSlurStart) {
                    token.slurStart = true;
                    pendingSlurStart = false;
                }
                if (pendingBeamStart) {
                    token.beamStart = true;
                    pendingBeamStart = false;
                }
                if (pendingTempoSpanner != TempoSpanner::None) {
                    token.tempoSpanner = pendingTempoSpanner;
                    pendingTempoSpanner = TempoSpanner::None;
                }
                if (pendingHairpin != Hairpin::None) {
                    token.hairpin = pendingHairpin;
                    pendingHairpin = Hairpin::None;
                }
                currentMeasure.events.push_back(token);
            }

            i = tokenEnd;
            continue;
        }

        // Normal note / rest / spacer token or standalone modifier
        int tokenEnd = i;
        while (tokenEnd < len && !notesText[tokenEnd].isSpace() && notesText[tokenEnd] != QLatin1Char('|') && notesText[tokenEnd] != QLatin1Char('}') && notesText[tokenEnd] != QLatin1Char('{')) {
            tokenEnd++;
        }

        QString tokenStr = notesText.mid(i, tokenEnd - i);

        // Check for standalone modifiers
        if (tokenStr == QLatin1String("@c")) {
            pendingMarker = SectionMarker::Chorus;
            i = tokenEnd;
            continue;
        }
        if (tokenStr == QLatin1String("@e")) {
            pendingMarker = SectionMarker::Coda;
            i = tokenEnd;
            continue;
        }
        if (tokenStr.startsWith(QLatin1String("@s")) && tokenStr.length() > 2) {
            pendingSharedSection = tokenStr.mid(2).toInt();
            i = tokenEnd;
            continue;
        }
        if (tokenStr == QLatin1String("(")) {
            pendingSlurStart = true;
            i = tokenEnd;
            continue;
        }
        if (tokenStr == QLatin1String("[")) {
            pendingBeamStart = true;
            i = tokenEnd;
            continue;
        }
        if (tokenStr == QLatin1String("\\rit")) {
            pendingTempoSpanner = TempoSpanner::Rit;
            i = tokenEnd;
            continue;
        }
        if (tokenStr == QLatin1String("\\accel")) {
            pendingTempoSpanner = TempoSpanner::Accel;
            i = tokenEnd;
            continue;
        }

        auto tok = NoteToken::fromString(tokenStr);
        if (!tok.has_value()) {
            Diagnostic diag;
            diag.severity = DiagnosticSeverity::Error;
            diag.code = QStringLiteral("INVALID_NOTE_TOKEN");
            diag.message = QStringLiteral("Invalid note token '%1' in measure %2").arg(tokenStr).arg(currentMeasureIndex);
            diag.partName = partName;
            diag.filePath = filePath;
            diag.measure = currentMeasureIndex;
            result.diagnostics.push_back(diag);
            result.hasErrors = true;
        } else {
            NoteToken token = tok.value();
            if (inTuplet) {
                token.duration.tupletN = tupletN;
                token.duration.tupletM = tupletM;
            }
            if (pendingMarker != SectionMarker::None) {
                token.sectionMarker = pendingMarker;
                pendingMarker = SectionMarker::None;
            }
            if (pendingSharedSection > 0) {
                token.sharedSectionIndex = pendingSharedSection;
                pendingSharedSection = 0;
            }
            if (pendingSlurStart) {
                token.slurStart = true;
                pendingSlurStart = false;
            }
            if (pendingBeamStart) {
                token.beamStart = true;
                pendingBeamStart = false;
            }
            if (pendingTempoSpanner != TempoSpanner::None) {
                token.tempoSpanner = pendingTempoSpanner;
                pendingTempoSpanner = TempoSpanner::None;
            }
            if (pendingHairpin != Hairpin::None) {
                token.hairpin = pendingHairpin;
                pendingHairpin = Hairpin::None;
            }
            currentMeasure.events.push_back(token);
        }

        i = tokenEnd;
    }

    // Add trailing measure if it has events
    if (!currentMeasure.events.empty() || result.measures.empty()) {
        result.measures.push_back(currentMeasure);
    }

    return result;
}

} // namespace OpenPsalm
