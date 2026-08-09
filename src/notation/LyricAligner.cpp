// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "LyricAligner.hpp"
#include <QRegularExpression>

namespace OpenPsalm {

std::vector<LyricSlot> LyricAligner::computeSlots(const std::vector<Measure>& measures) {
    std::vector<LyricSlot> outSlots;
    bool inSlurOrBeam = false;
    SectionMarker pendingMarker = SectionMarker::None;
    int pendingSharedSection = 0;
    int currentTick = 0;

    for (size_t mIdx = 0; mIdx < measures.size(); ++mIdx) {
        const Measure& m = measures[mIdx];
        for (size_t eIdx = 0; eIdx < m.events.size(); ++eIdx) {
            const NoteToken& ev = m.events[eIdx];

            // Update pending section markers
            if (ev.sectionMarker != SectionMarker::None) {
                pendingMarker = ev.sectionMarker;
            }
            if (ev.sharedSectionIndex > 0) {
                pendingSharedSection = ev.sharedSectionIndex;
            }

            if (ev.isSounding()) {
                LyricSlot slot;
                slot.measureIndex = m.index;
                slot.eventIndex = static_cast<int>(eIdx);
                slot.absoluteTick = currentTick;
                slot.sectionMarker = pendingMarker;
                slot.sharedSectionIndex = pendingSharedSection;
                slot.isSlurredMelisma = inSlurOrBeam;

                // Reset consumed pending markers
                pendingMarker = SectionMarker::None;
                pendingSharedSection = 0;

                outSlots.push_back(slot);
            }

            // Update slur/beam status for subsequent notes
            if (ev.slurStart || ev.beamStart || ev.dashedSlurStart) {
                inSlurOrBeam = true;
            }
            if (ev.slurEnd || ev.beamEnd || ev.dashedSlurEnd) {
                inSlurOrBeam = false;
            }

            currentTick += ev.duration.toInternalTicks();
        }
    }

    return outSlots;
}

QStringList LyricAligner::parseSyllables(const QString& rawLyrics) {
    QStringList result;
    QString normalized = rawLyrics.trimmed();
    if (normalized.isEmpty()) return result;

    // Split on whitespace or explicit ' -- '
    QStringList words = normalized.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString& w : words) {
        if (w == QLatin1String("--")) {
            continue; // Connector word delimiter
        }
        result.append(w);
    }

    return result;
}

QString LyricAligner::formatSyllables(const QStringList& syllables) {
    return syllables.join(QLatin1String(" "));
}

} // namespace OpenPsalm
