// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QString>
#include <optional>

namespace OpenPsalm {

enum class Accidental {
    Natural,
    Sharp,      // is
    Flat,       // es
    DoubleSharp,// isis
    DoubleFlat  // eses
};

struct Pitch {
    char step{'c'};             // 'a' through 'g'
    Accidental accidental{Accidental::Natural};
    int octave{3};              // OpenPsalm base octave is 3 (c = c3, c' = c4, c, = c2)

    QString toString() const;
    static std::optional<Pitch> fromString(const QString& str);

    // Semitone calculation (MIDI note number: C4 = 60)
    int midiNumber() const;
    static Pitch fromMidi(int midiNum);
    Pitch transposed(int semitones) const;

    // Staff position step (diatonic step count from C0)
    int staffDiatonicStep() const;

    bool operator==(const Pitch& other) const = default;
};

} // namespace OpenPsalm
