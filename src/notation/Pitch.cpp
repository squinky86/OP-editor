// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Pitch.hpp"

namespace OpenPsalm {

QString Pitch::toString() const {
    QString out;
    out.append(QChar(step));

    switch (accidental) {
        case Accidental::Natural: break;
        case Accidental::Sharp: out.append(QLatin1String("is")); break;
        case Accidental::Flat: out.append(QLatin1String("es")); break;
        case Accidental::DoubleSharp: out.append(QLatin1String("isis")); break;
        case Accidental::DoubleFlat: out.append(QLatin1String("eses")); break;
    }

    if (octave > 3) {
        out.append(QString(octave - 3, QLatin1Char('\'')));
    } else if (octave < 3) {
        out.append(QString(3 - octave, QLatin1Char(',')));
    }

    return out;
}

std::optional<Pitch> Pitch::fromString(const QString& str) {
    if (str.isEmpty()) return std::nullopt;

    char s = str[0].toLatin1();
    if (s < 'a' || s > 'g') return std::nullopt;

    Pitch p;
    p.step = s;
    p.octave = 3;
    p.accidental = Accidental::Natural;

    int idx = 1;
    // Check accidental
    if (str.mid(idx).startsWith(QLatin1String("isis"))) {
        p.accidental = Accidental::DoubleSharp;
        idx += 4;
    } else if (str.mid(idx).startsWith(QLatin1String("eses"))) {
        p.accidental = Accidental::DoubleFlat;
        idx += 4;
    } else if (str.mid(idx).startsWith(QLatin1String("is"))) {
        p.accidental = Accidental::Sharp;
        idx += 2;
    } else if (str.mid(idx).startsWith(QLatin1String("es"))) {
        p.accidental = Accidental::Flat;
        idx += 2;
    }

    // Check octave marks
    while (idx < str.length()) {
        QChar c = str[idx];
        if (c == QLatin1Char('\'')) {
            p.octave++;
            idx++;
        } else if (c == QLatin1Char(',')) {
            p.octave--;
            idx++;
        } else {
            return std::nullopt; // Extra invalid characters
        }
    }

    return p;
}

int Pitch::midiNumber() const {
    int baseSemitone = 0;
    switch (step) {
        case 'c': baseSemitone = 0; break;
        case 'd': baseSemitone = 2; break;
        case 'e': baseSemitone = 4; break;
        case 'f': baseSemitone = 5; break;
        case 'g': baseSemitone = 7; break;
        case 'a': baseSemitone = 9; break;
        case 'b': baseSemitone = 11; break;
    }

    int accOffset = 0;
    switch (accidental) {
        case Accidental::Natural: accOffset = 0; break;
        case Accidental::Sharp: accOffset = 1; break;
        case Accidental::Flat: accOffset = -1; break;
        case Accidental::DoubleSharp: accOffset = 2; break;
        case Accidental::DoubleFlat: accOffset = -2; break;
    }

    // C4 is octave 4, MIDI 60 (12 * (4 + 1) = 60)
    return (octave + 1) * 12 + baseSemitone + accOffset;
}

int Pitch::staffDiatonicStep() const {
    int stepOffset = 0;
    switch (step) {
        case 'c': stepOffset = 0; break;
        case 'd': stepOffset = 1; break;
        case 'e': stepOffset = 2; break;
        case 'f': stepOffset = 3; break;
        case 'g': stepOffset = 4; break;
        case 'a': stepOffset = 5; break;
        case 'b': stepOffset = 6; break;
    }
    return octave * 7 + stepOffset;
}

Pitch Pitch::fromMidi(int midiNum) {
    Pitch p;
    int octave = (midiNum / 12) - 1;
    int semitone = midiNum % 12;
    if (semitone < 0) {
        semitone += 12;
        octave -= 1;
    }

    p.octave = octave;
    switch (semitone) {
        case 0: p.step = 'c'; p.accidental = Accidental::Natural; break;
        case 1: p.step = 'c'; p.accidental = Accidental::Sharp; break;
        case 2: p.step = 'd'; p.accidental = Accidental::Natural; break;
        case 3: p.step = 'e'; p.accidental = Accidental::Flat; break;
        case 4: p.step = 'e'; p.accidental = Accidental::Natural; break;
        case 5: p.step = 'f'; p.accidental = Accidental::Natural; break;
        case 6: p.step = 'f'; p.accidental = Accidental::Sharp; break;
        case 7: p.step = 'g'; p.accidental = Accidental::Natural; break;
        case 8: p.step = 'a'; p.accidental = Accidental::Flat; break;
        case 9: p.step = 'a'; p.accidental = Accidental::Natural; break;
        case 10: p.step = 'b'; p.accidental = Accidental::Flat; break;
        case 11: p.step = 'b'; p.accidental = Accidental::Natural; break;
    }
    return p;
}

Pitch Pitch::transposed(int semitones) const {
    return fromMidi(midiNumber() + semitones);
}

} // namespace OpenPsalm
