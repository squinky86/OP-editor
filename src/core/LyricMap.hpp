// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QString>
#include <QMap>
#include <QStringList>

namespace OpenPsalm {

class LyricMap {
public:
    LyricMap() = default;

    void setSection(const QString& key, const QString& text);
    QString section(const QString& key) const;
    bool hasSection(const QString& key) const;
    void removeSection(const QString& key);
    void clear();

    QStringList keys() const;
    const QMap<QString, QString>& allSections() const { return m_sections; }

    // Helpers to classify section keys
    static bool isVerse(const QString& key);
    static bool isChorus(const QString& key);
    static bool isCoda(const QString& key);
    static bool isSharedSection(const QString& key);

private:
    QMap<QString, QString> m_sections;
};

} // namespace OpenPsalm
