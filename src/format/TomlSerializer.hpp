// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "core/SongData.hpp"
#include <QString>

namespace OpenPsalm {

class TomlSerializer {
public:
    static QString serialize(const SongData& song);
    static bool saveToFile(const SongData& song, const QString& filePath);
};

} // namespace OpenPsalm
