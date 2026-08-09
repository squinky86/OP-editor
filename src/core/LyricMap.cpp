// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "LyricMap.hpp"

namespace OpenPsalm {

void LyricMap::setSection(const QString& key, const QString& text) {
    m_sections[key] = text;
}

QString LyricMap::section(const QString& key) const {
    return m_sections.value(key);
}

bool LyricMap::hasSection(const QString& key) const {
    return m_sections.contains(key);
}

void LyricMap::removeSection(const QString& key) {
    m_sections.remove(key);
}

void LyricMap::clear() {
    m_sections.clear();
}

QStringList LyricMap::keys() const {
    return m_sections.keys();
}

bool LyricMap::isVerse(const QString& key) {
    bool ok = false;
    int v = key.toInt(&ok);
    return ok && v > 0;
}

bool LyricMap::isChorus(const QString& key) {
    return key.compare(QLatin1String("chorus"), Qt::CaseInsensitive) == 0;
}

bool LyricMap::isCoda(const QString& key) {
    return key.compare(QLatin1String("coda"), Qt::CaseInsensitive) == 0;
}

bool LyricMap::isSharedSection(const QString& key) {
    if (key.startsWith(QLatin1Char('s')) && key.length() > 1) {
        bool ok = false;
        int n = key.mid(1).toInt(&ok);
        return ok && n > 0;
    }
    return false;
}

} // namespace OpenPsalm
