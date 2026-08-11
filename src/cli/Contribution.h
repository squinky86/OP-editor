// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include "Cli.h"

#include <QByteArray>
#include <QString>

#include <expected>

namespace ope::contrib {

struct Request {
    QString outputParent;
    int workId = 0; ///< local corpus ID; ignored upstream for a new base song
    QString title;
    QString language;
    QString fileName;
    QString editorVersion;
    QByteArray proposedToml;
    QByteArray baselineToml; ///< empty only for a newly created file
    QByteArray baseToml;     ///< required when fileName is a translation overlay
    QByteArray copyrightFile;
};

struct Bundle {
    QString directory;
    QString archive;
    QString proposedFile;
    QString patchFile;
    QString reportFile;
    QString hashesFile;
    QString archiveSha256;
    cli::CheckSummary checks;
    bool newFile = false;
    bool newSong = false;
};

/// Validate the exact proposed bytes in an isolated song directory, then write
/// a review bundle and ZIP. No GitHub credentials or repository writes occur.
[[nodiscard]] std::expected<Bundle, QString> prepare(const Request &request);

} // namespace ope::contrib
