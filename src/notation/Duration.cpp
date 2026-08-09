// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Duration.hpp"

namespace OpenPsalm {

int Duration::toInternalTicks() const {
    int baseTicks = 0;
    switch (baseDuration) {
        case 1: baseTicks = Ticks::Whole; break;
        case 2: baseTicks = Ticks::Half; break;
        case 4: baseTicks = Ticks::Quarter; break;
        case 8: baseTicks = Ticks::Eighth; break;
        case 16: baseTicks = Ticks::Sixteenth; break;
        case 32: baseTicks = Ticks::ThirtySecond; break;
        case 64: baseTicks = Ticks::SixtyFourth; break;
        default: return 0;
    }

    int ticks = baseTicks;
    int addTicks = baseTicks / 2;
    for (int i = 0; i < dots; ++i) {
        ticks += addTicks;
        addTicks /= 2;
    }

    if (tupletN > 0 && tupletM > 0) {
        ticks = (ticks * tupletM) / tupletN;
    }

    return ticks;
}

int Duration::toPhraseTicks() const {
    return Ticks::internalToPhrase(toInternalTicks());
}

QString Duration::toString() const {
    QString out = QString::number(baseDuration);
    out.append(QString(dots, QLatin1Char('.')));
    return out;
}

std::optional<Duration> Duration::fromString(const QString& str) {
    if (str.isEmpty()) return std::nullopt;

    int idx = 0;
    while (idx < str.length() && str[idx].isDigit()) {
        idx++;
    }

    if (idx == 0) return std::nullopt;

    bool ok = false;
    int val = str.left(idx).toInt(&ok);
    if (!ok) return std::nullopt;

    if (val != 1 && val != 2 && val != 4 && val != 8 && val != 16 && val != 32 && val != 64) {
        return std::nullopt;
    }

    Duration d;
    d.baseDuration = val;
    d.dots = 0;

    while (idx < str.length() && str[idx] == QLatin1Char('.')) {
        d.dots++;
        idx++;
    }

    if (idx != str.length()) return std::nullopt;

    return d;
}

} // namespace OpenPsalm
