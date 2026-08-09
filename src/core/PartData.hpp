// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "Common.hpp"
#include "notation/NoteToken.hpp"
#include <QString>
#include <QMap>
#include <vector>

namespace OpenPsalm {

struct PartData {
    QString name;               // e.g. "Soprano", "Alto", "Tenor", "Bass"
    ChoralType choralType{ChoralType::Soprano};
    QString customChoralType;
    Clef clef{Clef::Treble};
    int staffNumber{1};         // 1-based staff grouping
    QString notesText;          // Raw multiline TOML notes string
    std::vector<Measure> parsedMeasures; // Cached parsed measures

    // Verse suppression
    std::vector<int> suppressVerses;
    QStringList suppressVersesWhen;

    // Per-part lyrics map: key -> lyrics text (e.g. "1", "chorus", etc.)
    QMap<QString, QString> lyrics;

    // Advanced preserved fields (e.g. splice_lyrics_into)
    QString spliceLyricsInto;

    // Translation overlay override tracking
    bool overridesChoralType{false};
    bool overridesClef{false};
    bool overridesStaffNumber{false};
    bool overridesNotes{false};
    bool overridesLyrics{false};
    bool overridesSuppressVerses{false};
    bool overridesSuppressVersesWhen{false};
    bool overridesSpliceLyricsInto{false};
};

} // namespace OpenPsalm
