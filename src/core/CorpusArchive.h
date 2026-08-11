// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include <QString>

#include <expected>
#include <functional>

namespace ope::corpus {

struct ExtractResult {
    int files = 0;
    qint64 bytes = 0;
    QString archiveRoot;
};

struct ArchiveLimits {
    qint64 maxEntries = 10'000;
    qint64 maxFileBytes = 32 * 1024 * 1024;
    qint64 maxExpandedBytes = 256 * 1024 * 1024;
};

/// Extract a GitHub source ZIP into an empty staging directory. The single
/// archive root (normally OP-songs-main/) is stripped. Paths, entry types, file
/// counts, and expanded sizes are checked before any corpus is installed.
[[nodiscard]] std::expected<ExtractResult, QString> extractSnapshotZip(
    const QString &archivePath, const QString &destination,
    const ArchiveLimits &limits = {});

struct InstallResult {
    QString target;
    QString backup; ///< previous target, retained for recovery; empty on first install
};

/// Optional platform-operation override used to prove rollback behavior under
/// deterministic rename failures. Production callers use the default.
struct InstallHooks {
    std::function<bool(const QString &source, const QString &destination)> renameDirectory;
};

/// Copy a validated staging tree beside `target`, then replace the target with
/// directory renames. If replacement fails, the previous directory is restored.
[[nodiscard]] std::expected<InstallResult, QString> installValidatedSnapshot(
    const QString &stagingRoot, const QString &target,
    const InstallHooks &hooks = {});

} // namespace ope::corpus
