// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "core/SongData.hpp"
#include "core/Common.hpp"
#include <vector>

namespace OpenPsalm {

class Validator {
public:
    static std::vector<Diagnostic> validateSong(const SongData& song, const QString& filePath = QString());
};

} // namespace OpenPsalm
