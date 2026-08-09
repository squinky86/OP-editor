// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "core/SongData.hpp"

namespace OpenPsalm {

class OverlayMerger {
public:
    static SongData merge(const SongData& baseSong, const SongData& overlaySong);
};

} // namespace OpenPsalm
