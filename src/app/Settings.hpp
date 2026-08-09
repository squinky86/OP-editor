// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace OpenPsalm {

class Settings {
public:
    static QString openPsalmSongsPath();
    static void setOpenPsalmSongsPath(const QString& path);

    static QStringList recentFiles();
    static void addRecentFile(const QString& path);
};

} // namespace OpenPsalm
