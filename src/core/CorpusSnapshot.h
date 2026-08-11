// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <expected>

namespace ope::corpus {

inline constexpr auto SnapshotFileName = ".openpsalm-snapshot.json";

struct SnapshotInfo {
    int schema = 2;
    QString repository = QStringLiteral("squinky86/OP-songs");
    QString branch = QStringLiteral("main");
    QString commitSha;
    QDateTime commitDate;
    QDateTime currentAsOf;
    QDateTime downloadedAt;
    QString requestedUrl;
    QString downloadUrl;
    QString archiveSha256;
    QString httpEtag;
    QString archiveRoot;
    QString editorVersion;

    [[nodiscard]] bool hasResolvedCommit() const noexcept;
};

[[nodiscard]] QString snapshotPath(const QString &corpusRoot);
[[nodiscard]] std::expected<SnapshotInfo, QString> readSnapshot(const QString &corpusRoot);
[[nodiscard]] std::expected<void, QString> writeSnapshot(
    const QString &corpusRoot, const SnapshotInfo &snapshot);

/// Update freshness only when the installed commit still equals resolved HEAD.
[[nodiscard]] std::expected<void, QString> markSnapshotCurrent(const QString &corpusRoot,
    const QString &headSha, const QDateTime &headCommitDate, const QDateTime &checkedAt);

/// Return timestamped backup directories adjacent to `target`, newest first.
[[nodiscard]] QStringList backupDirectories(const QString &target);

[[nodiscard]] QString abbreviatedSha(const QString &sha);

} // namespace ope::corpus
