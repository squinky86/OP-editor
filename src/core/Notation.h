// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The OpenPsalm note stream: pitches, durations, flags, and the tick arithmetic
// that decides whether a measure is legal.
//
// This is a port of OpenPsalm's `src/seed/parser.rs`, quirks included. Where the
// Rust parser is silently permissive — an unknown duration becoming a quarter
// note, an unrecognised `\word` swallowed into the duration string — this port
// reproduces the behaviour *and* records a diagnostic, because the seeder will
// import the garbage without complaint.

#pragma once

#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace ope {

// -- ticks --------------------------------------------------------------------

/// Internal resolution, matching the seeder: whole = 192, quarter = 48, so a
/// triplet eighth (24 * 2/3) stays an integer at 16.
namespace ticks {
inline constexpr int Whole = 192;
inline constexpr int Half = 96;
inline constexpr int Quarter = 48;
inline constexpr int Eighth = 24;
inline constexpr int Sixteenth = 12;
inline constexpr int ThirtySecond = 6;
inline constexpr int SixtyFourth = 3;

/// Phrase-break strings count 64ths (quarter = 16). Three internal ticks each.
inline constexpr int PerPhraseTick = 3;
inline constexpr std::array<int, 7> TimeSignatureDenominators { 1, 2, 4, 8, 16, 32, 64 };

[[nodiscard]] constexpr bool isSupportedTimeSignatureDenominator(int denominator) noexcept
{
    for (const int supported : TimeSignatureDenominators) {
        if (denominator == supported)
            return true;
    }
    return false;
}

[[nodiscard]] constexpr int toPhraseTicks(int internal) noexcept
{
    return internal / PerPhraseTick;
}
[[nodiscard]] constexpr int fromPhraseTicks(int phrase) noexcept
{
    return phrase * PerPhraseTick;
}
[[nodiscard]] constexpr int forTimeSignature(int numerator, int denominator) noexcept
{
    const int unit = denominator == 1 ? Whole
        : denominator == 2            ? Half
        : denominator == 4            ? Quarter
        : denominator == 8            ? Eighth
        : denominator == 16           ? Sixteenth
        : denominator == 32           ? ThirtySecond
        : denominator == 64           ? SixtyFourth
                                      : Quarter;
    return unit * numerator;
}
} // namespace ticks

// -- pitch --------------------------------------------------------------------

struct Pitch {
    QChar step;      ///< upper-case 'A'..'G'
    int octave = 3;  ///< scientific pitch; no octave mark means 3
    int alter = 0;   ///< -2..+2

    [[nodiscard]] bool isValid() const noexcept { return !step.isNull(); }
    /// MIDI note number, matching `pitch_to_midi` in src/export/midi.rs.
    [[nodiscard]] int midiNote() const noexcept;
    /// Diatonic index: octave * 7 + step, for comparisons and transposition.
    [[nodiscard]] int diatonic() const noexcept;
    [[nodiscard]] QString toToken() const;
    [[nodiscard]] bool operator==(const Pitch &) const = default;

    static Pitch fromToken(QStringView token);
    static Pitch fromDiatonic(int diatonic, int alter);
};

// -- duration -----------------------------------------------------------------

struct Duration {
    int base = 4;  ///< 1, 2, 4, 8, 16, 32, 64
    int dots = 0;

    [[nodiscard]] int baseTicks() const noexcept;
    [[nodiscard]] int notatedTicks() const noexcept;
    [[nodiscard]] QString toToken() const;
    [[nodiscard]] bool operator==(const Duration &) const = default;
    /// The seeder's duration-name mapping; anything unknown becomes a quarter.
    [[nodiscard]] static QString typeName(int base);
};

// -- one event ----------------------------------------------------------------

enum class EventKind { Note, Chord, Rest, Spacer };

struct Tuplet {
    int actual = 3;
    int normal = 2;
    bool isStart = false;
    bool isEnd = false;
};

/// One rhythmic position in a part: a note, a chord (divisi), a rest, or a
/// spacer. `raw` is the verbatim source token and is re-emitted untouched unless
/// the event is edited, which is what keeps saved files byte-identical.
struct Event {
    QString raw;
    bool dirty = false;

    EventKind kind = EventKind::Note;
    QList<Pitch> pitches;  ///< >1 means a chord; empty for rest/spacer
    Duration duration;
    std::optional<Tuplet> tuplet;

    bool tie = false;
    bool slurStart = false;
    bool slurEnd = false;
    bool dashedSlurStart = false;
    bool dashedSlurEnd = false;
    bool beamStart = false;
    bool beamEnd = false;
    bool fermata = false;
    bool staccato = false;
    bool accent = false;
    bool marcato = false;
    bool chorusStart = false;
    bool codaStart = false;
    QString dynamic;       ///< without the '%'
    QString hairpin;       ///< "crescendo" | "diminuendo" | "end"
    QString tempoSpanner;  ///< "rit" | "ritard" | "rall" | "accel" | "string" | "atempo"
    bool spannerEnd = false;
    int dedupOffset = 0;

    // Derived by NoteStream; not part of the source text.
    int tickInMeasure = 0;
    int slotIndex = -1;
    int measureIndex = 0;
    int indexInMeasure = 0;

    [[nodiscard]] bool isRest() const noexcept { return kind == EventKind::Rest; }
    [[nodiscard]] bool isSpacer() const noexcept { return kind == EventKind::Spacer; }
    [[nodiscard]] bool isSounding() const noexcept
    {
        return kind == EventKind::Note || kind == EventKind::Chord;
    }
    /// Played length, with the tuplet ratio applied the way the seeder does it:
    /// integer division, truncating.
    [[nodiscard]] int playedTicks() const noexcept;
    /// Regenerate the token text from the parsed fields, in OPE's canonical
    /// suffix order. The seeder's stripping loop is order-independent, so any
    /// order parses back identically.
    [[nodiscard]] QString toSource() const;
    /// The token text to write: `raw` when clean, freshly emitted when dirty.
    [[nodiscard]] QString text() const { return dirty || raw.isEmpty() ? toSource() : raw; }
};

/// A diagnostic raised while tokenizing, reported against a measure and event.
struct TokenIssue {
    enum class Code {
        MissingDuration,
        UnknownDuration,
        UnknownDynamic,
        UndocumentedDynamic,
        UnknownSpanner,
        EmptyChord,
        UnterminatedChord,
        UnterminatedTuplet,
        StrayCloseBrace,
        LeftoverText,
    };
    Code code {};
    int measureIndex = 0;
    int eventIndex = 0;
    QString token;
    QString detail;
};

/// One measure of events, plus the line layout it was written with.
struct Measure {
    QList<Event> events;

    [[nodiscard]] int notatedTicks() const;
    [[nodiscard]] int playedTicks() const;
};

/// A part's whole note stream: measures, plus the source line grouping so an
/// edited `notes` block keeps the shape the author gave it.
class NoteStream {
public:
    /// Tokenize a `notes` string. Never fails: unparseable fragments become
    /// issues, mirroring a seeder that imports whatever it managed to read.
    static NoteStream parse(const QString &notes, QList<TokenIssue> *issues = nullptr);

    [[nodiscard]] const QList<Measure> &measures() const noexcept { return m_measures; }
    [[nodiscard]] QList<Measure> &measures() noexcept { return m_measures; }
    [[nodiscard]] qsizetype measureCount() const noexcept { return m_measures.size(); }

    /// Measure counts of each source line, e.g. {4, 4, 4, 5}.
    [[nodiscard]] const QList<int> &lineLayout() const noexcept { return m_lineLayout; }
    void setLineLayout(QList<int> layout) { m_lineLayout = std::move(layout); }

    /// Recompute tick positions, slot indices, and measure/event back-pointers.
    void reindex();

    /// Serialize back to a `notes` body, preserving the line grouping when the
    /// measure count still matches and falling back to 4 measures per line.
    [[nodiscard]] QString toSource() const;

    [[nodiscard]] bool anyDirty() const;

private:
    QList<Measure> m_measures;
    QList<int> m_lineLayout;
};

/// Lyric-slot state machine, ported from `LyricSlotState::is_lyric_slot`.
/// A note is a slot only when it begins a fresh melisma.
class SlotCounter {
public:
    [[nodiscard]] bool isSlot(const Event &event);
    void reset() { *this = SlotCounter {}; }

private:
    bool m_inSlur = false;
    bool m_inBeam = false;
    bool m_tieActive = false;
};

/// The documented ten dynamics, in printed order.
[[nodiscard]] QStringList documentedDynamics();
/// Everything `dynamic_to_velocity` in src/export/midi.rs recognises.
[[nodiscard]] QStringList recognisedDynamics();
/// Note-on velocity for a dynamic name; unknown names give 80, as the exporter does.
[[nodiscard]] int velocityForDynamic(QStringView name);
/// Canonical tempo-spanner names, longest first (so `\ritard` beats `\rit`).
[[nodiscard]] QStringList tempoSpannerNames();

} // namespace ope
