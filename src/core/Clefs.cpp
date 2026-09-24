// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
#include "Clefs.h"

namespace ope::clefs {

const QList<Clef> &all()
{
    static const QList<Clef> values {
        { QStringLiteral("treble"), QStringLiteral("Treble"), Sign::G, 2, 0, 30 },
        { QStringLiteral("bass"), QStringLiteral("Bass"), Sign::F, 4, 0, 18 },
        { QStringLiteral("treble_8"), QStringLiteral("Treble 8vb"), Sign::G, 2, -1, 23 },
        { QStringLiteral("alto"), QStringLiteral("Alto (C on line 3)"), Sign::C, 3, 0, 24 },
        { QStringLiteral("tenor"), QStringLiteral("Tenor (C on line 4)"), Sign::C, 4, 0, 22 },
    };
    return values;
}

QStringList names()
{
    QStringList result;
    for (const Clef &clef : all())
        result.append(clef.name);
    return result;
}

std::optional<Clef> parse(QStringView value)
{
    const QString normalized = value.trimmed().toString().toLower();
    for (const Clef &clef : all()) {
        if (clef.name == normalized)
            return clef;
    }
    return std::nullopt;
}

std::optional<Clef> effective(const std::optional<QString> &value)
{
    return parse(value.value_or(QStringLiteral("treble")));
}

} // namespace ope::clefs
