// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Settings.hpp"
#include <QSettings>
#include <QDir>

namespace OpenPsalm {

static const char* KEY_SONGS_PATH = "OpenPsalm/SongsPath";
static const char* KEY_RECENT_FILES = "Editor/RecentFiles";

QString Settings::openPsalmSongsPath() {
    QSettings settings;
    return settings.value(QLatin1String(KEY_SONGS_PATH), QDir::homePath() + QStringLiteral("/OpenPsalm/songs")).toString();
}

void Settings::setOpenPsalmSongsPath(const QString& path) {
    QSettings settings;
    settings.setValue(QLatin1String(KEY_SONGS_PATH), path);
}

QStringList Settings::recentFiles() {
    QSettings settings;
    return settings.value(QLatin1String(KEY_RECENT_FILES)).toStringList();
}

void Settings::addRecentFile(const QString& path) {
    QSettings settings;
    QStringList files = settings.value(QLatin1String(KEY_RECENT_FILES)).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > 10) {
        files.removeLast();
    }
    settings.setValue(QLatin1String(KEY_RECENT_FILES), files);
}

} // namespace OpenPsalm
