// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "NoteToken.hpp"
#include <QString>
#include <vector>

namespace OpenPsalm {

class NoteSerializer {
public:
    static QString serialize(const std::vector<Measure>& measures, int measuresPerLine = 4);
};

} // namespace OpenPsalm
