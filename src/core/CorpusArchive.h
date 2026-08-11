// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include <QString>

#include <expected>

namespace ope::corpus {

inline constexpr auto HeadArchiveUrl
    = "https://github.com/squinky86/OP-songs/archive/refs/heads/main.zip";

struct ExtractResult {
    int files = 0;
    qint64 bytes = 0;
    QString archiveRoot;
};

/// Extract a GitHub source ZIP into an empty staging directory. The single
/// archive root (normally OP-songs-main/) is stripped. Paths, entry types, file
/// counts, and expanded sizes are checked before any corpus is installed.
[[nodiscard]] std::expected<ExtractResult, QString> extractSnapshotZip(
    const QString &archivePath, const QString &destination);

struct InstallResult {
    QString target;
    QString backup; ///< previous target, retained for recovery; empty on first install
};

/// Copy a validated staging tree beside `target`, then replace the target with
/// directory renames. If replacement fails, the previous directory is restored.
[[nodiscard]] std::expected<InstallResult, QString> installValidatedSnapshot(
    const QString &stagingRoot, const QString &target);

} // namespace ope::corpus
