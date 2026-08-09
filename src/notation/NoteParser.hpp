// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "NoteToken.hpp"
#include "core/Common.hpp"
#include <QString>
#include <vector>

namespace OpenPsalm {

struct ParseResult {
    std::vector<Measure> measures;
    std::vector<Diagnostic> diagnostics;
    bool hasErrors{false};
};

class NoteParser {
public:
    static ParseResult parse(const QString& notesText, const QString& partName = QString(), const QString& filePath = QString());
};

} // namespace OpenPsalm
