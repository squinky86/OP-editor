// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "core/SongData.hpp"
#include "core/Common.hpp"
#include <QString>
#include <vector>

namespace OpenPsalm {

struct LoadResult {
    SongData songData;
    std::vector<Diagnostic> diagnostics;
    bool success{true};
};

class TomlLoader {
public:
    static LoadResult loadFile(const QString& filePath);
    static LoadResult loadFromString(const QString& tomlContent, const QString& filePath = QString());
};

} // namespace OpenPsalm
