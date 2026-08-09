// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Common.hpp"

namespace OpenPsalm {

QString clefToString(Clef clef) {
    switch (clef) {
        case Clef::Treble: return QStringLiteral("treble");
        case Clef::Bass: return QStringLiteral("bass");
        case Clef::Treble8: return QStringLiteral("treble_8");
    }
    return QStringLiteral("treble");
}

std::optional<Clef> clefFromString(const QString& str) {
    QString lower = str.toLower().trimmed();
    if (lower == QLatin1String("treble")) return Clef::Treble;
    if (lower == QLatin1String("bass")) return Clef::Bass;
    if (lower == QLatin1String("treble_8")) return Clef::Treble8;
    return std::nullopt;
}

QString choralTypeToString(ChoralType type, const QString& customName) {
    switch (type) {
        case ChoralType::Soprano: return QStringLiteral("soprano");
        case ChoralType::Alto: return QStringLiteral("alto");
        case ChoralType::Tenor: return QStringLiteral("tenor");
        case ChoralType::Bass: return QStringLiteral("bass");
        case ChoralType::Custom: return customName;
    }
    return QStringLiteral("soprano");
}

ChoralType choralTypeFromString(const QString& str) {
    QString lower = str.toLower().trimmed();
    if (lower == QLatin1String("soprano")) return ChoralType::Soprano;
    if (lower == QLatin1String("alto")) return ChoralType::Alto;
    if (lower == QLatin1String("tenor")) return ChoralType::Tenor;
    if (lower == QLatin1String("bass")) return ChoralType::Bass;
    return ChoralType::Custom;
}

QString PhraseBreak::toString() const {
    return QStringLiteral("%1:%2").arg(measure).arg(phraseTicks);
}

std::optional<PhraseBreak> PhraseBreak::fromString(const QString& str, PhraseBreakKind kind) {
    QString trimmed = str.trimmed();
    int colonIdx = trimmed.indexOf(QLatin1Char(':'));
    if (colonIdx <= 0 || colonIdx == trimmed.length() - 1) {
        return std::nullopt;
    }

    bool ok1 = false, ok2 = false;
    int m = trimmed.left(colonIdx).toInt(&ok1);
    int t = trimmed.mid(colonIdx + 1).toInt(&ok2);

    if (!ok1 || !ok2 || m < 1 || t < 0) {
        return std::nullopt;
    }

    return PhraseBreak{kind, m, t};
}

QString Diagnostic::severityString() const {
    switch (severity) {
        case DiagnosticSeverity::Info: return QStringLiteral("Info");
        case DiagnosticSeverity::Warning: return QStringLiteral("Warning");
        case DiagnosticSeverity::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Error");
}

} // namespace OpenPsalm
