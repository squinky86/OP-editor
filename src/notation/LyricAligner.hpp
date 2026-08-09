// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "NoteToken.hpp"
#include "core/Common.hpp"
#include <QString>
#include <QStringList>
#include <vector>

namespace OpenPsalm {

struct LyricSlot {
    int measureIndex{1};
    int eventIndex{0};
    int absoluteTick{0};
    SectionMarker sectionMarker{SectionMarker::None};
    int sharedSectionIndex{0};
    bool isSlurredMelisma{false};
};

struct LyricSectionSlots {
    QString sectionKey; // "1", "2", "chorus", "coda", "s1", etc.
    std::vector<LyricSlot> lyricSlots;
};

class LyricAligner {
public:
    // Computes the lyric slots from parsed measures for a part
    static std::vector<LyricSlot> computeSlots(const std::vector<Measure>& measures);

    // Parses a raw lyrics string into individual syllables
    static QStringList parseSyllables(const QString& rawLyrics);

    // Formats syllables back to text with -- delimiters
    static QString formatSyllables(const QStringList& syllables);
};

} // namespace OpenPsalm
