// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SongData.hpp"
#include <algorithm>

namespace OpenPsalm {

QStringList SongData::partNamesInOrder() const {
    QStringList standardOrder = {
        QStringLiteral("Soprano"),
        QStringLiteral("Alto"),
        QStringLiteral("Tenor"),
        QStringLiteral("Bass")
    };

    QStringList result;
    // 1. Add matching standard parts in SATB order
    for (const QString& name : standardOrder) {
        for (auto it = parts.begin(); it != parts.end(); ++it) {
            if (it.key().compare(name, Qt::CaseInsensitive) == 0 && !result.contains(it.key())) {
                result.append(it.key());
            }
        }
    }

    // 2. Add remaining custom parts alphabetically
    QStringList remaining;
    for (auto it = parts.begin(); it != parts.end(); ++it) {
        if (!result.contains(it.key())) {
            remaining.append(it.key());
        }
    }
    std::sort(remaining.begin(), remaining.end());
    result.append(remaining);

    return result;
}

int SongData::maxMeasureCount() const {
    int maxM = 0;
    for (const auto& p : parts) {
        maxM = std::max(maxM, static_cast<int>(p.parsedMeasures.size()));
    }
    return maxM;
}

} // namespace OpenPsalm
