// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "Pitch.hpp"
#include "Duration.hpp"
#include <QString>
#include <vector>
#include <optional>

namespace OpenPsalm {

enum class NoteKind {
    Note,
    Rest,
    Spacer,
    Chord
};

enum class SectionMarker {
    None,
    Chorus, // @c
    Coda    // @e
};

enum class Dynamic {
    None,
    PPP, // %ppp
    PP,  // %pp
    P,   // %p
    MP,  // %mp
    MF,  // %mf
    F,   // %f
    FF,  // %ff
    FFF, // %fff
    FP,  // %fp
    SFZ  // %sfz
};

enum class Hairpin {
    None,
    CrescendoStart,   // \<
    DecrescendoStart, // \>
    End               // \!
};

enum class TempoSpanner {
    None,
    Rit,     // \rit
    Ritard,  // \ritard
    Rall,    // \rall
    Accel,   // \accel
    String,  // \string
    Atempo,  // \atempo
    SpanEnd  // \spanend
};

struct NoteToken {
    NoteKind kind{NoteKind::Note};
    std::vector<Pitch> pitches; // 1 pitch for Note, multiple for Chord, empty for Rest/Spacer
    Duration duration;

    // Articulations & Slurs
    bool slurStart{false};       // (
    bool slurEnd{false};         // )
    bool dashedSlurStart{false}; // -(
    bool dashedSlurEnd{false};   // -)
    bool beamStart{false};       // [
    bool beamEnd{false};         // ]
    bool tie{false};             // ~
    bool fermata{false};         // !
    bool staccato{false};        // -.

    // Section markers
    SectionMarker sectionMarker{SectionMarker::None};
    int sharedSectionIndex{0};   // @s1, @s2, etc. (0 if none)

    // Expression & Dynamics
    Dynamic dynamic{Dynamic::None};
    Hairpin hairpin{Hairpin::None};
    TempoSpanner tempoSpanner{TempoSpanner::None};

    // Deduplication offset
    std::optional<int> dedupOffset; // /N, /+N, /-N

    // Original raw source representation for fidelity
    QString rawToken;

    // Checks
    bool isSounding() const { return kind == NoteKind::Note || kind == NoteKind::Chord; }
    bool consumesLyric() const { return isSounding(); }

    QString toString() const;
    static std::optional<NoteToken> fromString(const QString& tokenStr);
};

struct Measure {
    int index{1}; // 1-based measure index
    std::vector<NoteToken> events;

    int totalInternalTicks() const;
    int totalPhraseTicks() const;
    QString toString() const;
};

} // namespace OpenPsalm
