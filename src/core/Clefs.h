// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace ope::clefs {

enum class Sign { G, F, C };

/// Display geometry only. Notation and playback always use sounding pitches.
struct Clef {
    QString name;
    QString label;
    Sign sign;
    int line;                  ///< reference line, counted from the bottom
    int octaveChange;
    int bottomLineDiatonic;     ///< Pitch::diatonic() of the sounding bottom line
    bool operator==(const Clef &) const = default;
};

[[nodiscard]] const QList<Clef> &all();
[[nodiscard]] QStringList names();
[[nodiscard]] std::optional<Clef> parse(QStringView value);
/// An omitted clef means treble; an explicit unsupported value stays invalid.
[[nodiscard]] std::optional<Clef> effective(const std::optional<QString> &value);

} // namespace ope::clefs
