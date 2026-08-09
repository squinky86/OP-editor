// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "core/Common.hpp"
#include <QString>
#include <optional>

namespace OpenPsalm {

struct Duration {
    int baseDuration{4}; // 1, 2, 4, 8, 16, 32, 64
    int dots{0};         // Number of dots (0, 1, 2)
    int tupletN{0};      // Tuplet actual count (e.g. 3 in 3:2)
    int tupletM{0};      // Tuplet in-the-time-of count (e.g. 2 in 3:2)

    int toInternalTicks() const;
    int toPhraseTicks() const;

    QString toString() const;
    static std::optional<Duration> fromString(const QString& str);

    bool operator==(const Duration& other) const = default;
};

} // namespace OpenPsalm
