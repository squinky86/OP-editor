// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Every check OPE runs on a song, in three tiers:
//
//   * errors the Rust seeder would raise, worded the way it words them;
//   * errors the seeder *accepts* and then gets wrong (a truncated verse, a
//     typo'd duration that silently becomes a quarter note) — the class that
//     motivates this whole program;
//   * style-guide warnings, carrying the same rule numbers as
//     OpenPsalm's tools/audit_style.py so the two tools can be compared.

#pragma once

#include "Lyrics.h"
#include "Song.h"

#include <QList>
#include <QString>

namespace ope {

enum class Severity { Error, Warning, Info };

struct Finding {
    Severity severity = Severity::Warning;
    QString rule;      ///< "E-MEASURE", "R1.1", …
    QString message;
    QString partName;
    int measure = -1;      ///< 1-based, -1 when not measure-specific
    int eventIndex = -1;
    QString lyricKey;
    int slot = -1;
    QString fixHint;   ///< non-empty when a quick fix exists

    [[nodiscard]] QString location() const;
    [[nodiscard]] QString formatted() const;
};

/// Run every rule. `languageKnown` reports whether the document's language code
/// is in the bundled registry; the caller supplies it so the core stays free of
/// registry lookups.
[[nodiscard]] QList<Finding> validate(const SongDocument &doc, bool languageKnown = true,
    const QString &baseLanguage = QString());

[[nodiscard]] int countBySeverity(const QList<Finding> &findings, Severity severity);

/// Valid values for the enumerated header and part fields.
[[nodiscard]] QStringList validKeySignatures();
[[nodiscard]] QStringList validClefs();
[[nodiscard]] QStringList validChoralTypes();

} // namespace ope
