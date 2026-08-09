// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QMetaType>
#include <optional>
#include <vector>

namespace OpenPsalm {

enum class Clef {
    Treble,
    Bass,
    Treble8
};

QString clefToString(Clef clef);
std::optional<Clef> clefFromString(const QString& str);

enum class ChoralType {
    Soprano,
    Alto,
    Tenor,
    Bass,
    Custom
};

QString choralTypeToString(ChoralType type, const QString& customName = QString());
ChoralType choralTypeFromString(const QString& str);

enum class PhraseBreakKind {
    Required,       // phrase_breaks: poetic, sheet/slide, and dedup
    Optional,       // optional_phrase_breaks: optional visual/poetry and dedup
    NonBreaking     // non_breaking_phrase_breaks: dedup only
};

struct PhraseBreak {
    PhraseBreakKind kind{PhraseBreakKind::Required};
    int measure{1};     // 1-based measure index
    int phraseTicks{0}; // 64th-note ticks inside measure (0..N)

    QString toString() const;
    static std::optional<PhraseBreak> fromString(const QString& str, PhraseBreakKind kind = PhraseBreakKind::Required);
};

struct TimeSignatureChange {
    int measure{1};     // 1-based measure index
    int numerator{4};
    int denominator{4};
    int duration{0};    // Duration in measures
};

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct SourceSpan {
    int startLine{1};
    int startColumn{1};
    int endLine{1};
    int endColumn{1};
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    QString code;
    QString message;
    QString filePath;
    std::optional<SourceSpan> span;
    QString partName;
    int measure{0};

    QString severityString() const;
};

// Tick constants for OpenPsalm format
namespace Ticks {
    // Internal duration ticks (scaled by 3 for exact tuplet calculations)
    constexpr int Whole = 192;
    constexpr int Half = 96;
    constexpr int Quarter = 48;
    constexpr int Eighth = 24;
    constexpr int Sixteenth = 12;
    constexpr int ThirtySecond = 6;
    constexpr int SixtyFourth = 3;

    // Phrase-break ticks (64th notes)
    constexpr int PhraseWhole = 64;
    constexpr int PhraseHalf = 32;
    constexpr int PhraseQuarter = 16;
    constexpr int PhraseEighth = 8;
    constexpr int PhraseSixteenth = 4;
    constexpr int PhraseThirtySecond = 2;
    constexpr int PhraseSixtyFourth = 1;

    constexpr int internalToPhrase(int internalTicks) {
        return internalTicks / 3;
    }

    constexpr int phraseToInternal(int phraseTicks) {
        return phraseTicks * 3;
    }
}

} // namespace OpenPsalm
