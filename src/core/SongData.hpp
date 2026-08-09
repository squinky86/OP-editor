// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "Common.hpp"
#include "PartData.hpp"
#include "LyricMap.hpp"
#include <QString>
#include <QStringList>
#include <QMap>
#include <vector>
#include <optional>

namespace OpenPsalm {

struct SongData {
    // Required base metadata
    QString title;
    std::optional<QString> subtitle;
    bool active{true};
    QString language{QStringLiteral("en")};
    int verseCount{1};
    QString keySignature{QStringLiteral("C")};
    int timeSigNumerator{4};
    int timeSigDenominator{4};
    int tempoBpm{100};

    // Optional metadata
    QStringList copyrights;
    std::optional<QString> commentary;
    std::vector<int> convergeVerses;

    // Time signature changes & phrase breaks
    std::vector<TimeSignatureChange> timeSigChanges;
    std::vector<PhraseBreak> phraseBreaks;
    std::vector<PhraseBreak> optionalPhraseBreaks;
    std::vector<PhraseBreak> nonBreakingPhraseBreaks;

    // Parts (e.g. Soprano, Alto, Tenor, Bass)
    QMap<QString, PartData> parts;

    // Global Lyrics
    LyricMap lyrics;

    // Translation overlay tracking flags
    bool isTranslationOverlay{false};
    bool overridesTitle{false};
    bool overridesSubtitle{false};
    bool overridesActive{false};
    bool overridesLanguage{false};
    bool overridesVerseCount{false};
    bool overridesKeySignature{false};
    bool overridesTimeSig{false};
    bool overridesTempoBpm{false};
    bool overridesCopyrights{false};
    bool overridesCommentary{false};
    bool overridesTimeSigChanges{false};
    bool overridesPhraseBreaks{false};
    bool overridesOptionalPhraseBreaks{false};
    bool overridesNonBreakingPhraseBreaks{false};
    bool overridesLyrics{false};

    // Helper methods
    QStringList partNamesInOrder() const;
    int maxMeasureCount() const;
};

} // namespace OpenPsalm
