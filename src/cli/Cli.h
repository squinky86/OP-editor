// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Headless checks over a songs directory, so the format layer can be exercised
// (and regressions caught in CI) without starting a GUI.

#pragma once

#include <QStringList>

namespace ope::cli {

struct Options {
    QString root;          ///< songs directory or a single .toml file
    bool checkRoundTrip = true;
    bool checkReemit = true;
    bool validate = true;
    bool warnings = true;
    bool info = false;
    bool quiet = false;
    int limit = -1;        ///< stop after N songs; -1 = all
};

/// Machine-readable result used by the GUI before installing a downloaded
/// corpus. These are the same gates printed by `ope-check`.
struct CheckSummary {
    int files = 0;
    int parseFailures = 0;
    int roundTripFailures = 0;
    int reemitFailures = 0;
    int errors = 0;
    int warnings = 0;
    int infos = 0;
    int songsWithErrors = 0;
    bool foundCorpus = true;

    [[nodiscard]] bool passed() const noexcept;
    [[nodiscard]] QString description() const;
};

/// Run all enabled checks without writing to stdout.
[[nodiscard]] CheckSummary check(const Options &options);

/// Run the checks. Returns a process exit code: 0 when nothing failed.
[[nodiscard]] int run(const Options &options);

/// Parse `--` arguments into Options. Returns false when usage was printed.
[[nodiscard]] bool parse(const QStringList &arguments, Options &options, int &exitCode);

[[nodiscard]] QString usage();

} // namespace ope::cli
