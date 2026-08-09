// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Library.h"

#include "Song.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace ope {

namespace i18n {

QString defaultLanguage() { return QStringLiteral("en"); }

const QList<LanguageInfo> &languages()
{
    // Ordinal 0 is reserved for "whatever language the base file is in", so
    // registry ordinals start at 1 — English included. Append only.
    static const QList<LanguageInfo> registry {
        { QStringLiteral("en"), 1, QStringLiteral("English"), QStringLiteral("English") },
        { QStringLiteral("es"), 2, QStringLiteral("Spanish"), QStringLiteral("Español") },
    };
    return registry;
}

const LanguageInfo *lookup(QStringView code)
{
    for (const LanguageInfo &language : languages()) {
        if (language.code == code)
            return &language;
    }
    return nullptr;
}

bool isKnown(QStringView code) { return lookup(code) != nullptr; }

QString codeFromFilename(QStringView filename)
{
    if (!filename.startsWith(QLatin1String("song_")) || !filename.endsWith(QLatin1String(".toml")))
        return {};
    const qsizetype length = filename.size() - 5 - 5;
    if (length <= 0)
        return {};
    return filename.sliced(5, length).toString();
}

int baseRowId(int workId) { return workId; }

int translationRowId(int workId, QStringView code)
{
    const LanguageInfo *language = lookup(code);
    if (!language)
        return 0;
    return language->ordinal * IdStride + workId;
}

QString registrySnippet(QStringView code, QStringView englishName, QStringView nativeName)
{
    int nextOrdinal = 0;
    for (const LanguageInfo &language : languages())
        nextOrdinal = std::max(nextOrdinal, language.ordinal);
    ++nextOrdinal;
    return QStringLiteral("Language {\n"
                          "    code: \"%1\",\n"
                          "    ordinal: %2,\n"
                          "    english_name: \"%3\",\n"
                          "    native_name: \"%4\",\n"
                          "},")
        .arg(code.toString())
        .arg(nextOrdinal)
        .arg(englishName.toString(), nativeName.toString());
}

} // namespace i18n

QString SongEntry::displayTitle() const
{
    if (!problem.isEmpty())
        return QStringLiteral("(unreadable)");
    return title.isEmpty() ? QStringLiteral("(untitled)") : title;
}

QStringList SongEntry::allLanguages() const
{
    QStringList out;
    if (!baseLanguage.isEmpty())
        out.append(baseLanguage);
    out.append(translationLanguages);
    return out;
}

QString SongEntry::pathForLanguage(QStringView code) const
{
    if (code.isEmpty() || code == baseLanguage)
        return basePath;
    for (qsizetype i = 0; i < translationLanguages.size(); ++i) {
        if (translationLanguages.at(i) == code)
            return translationPaths.at(i);
    }
    return {};
}

bool Library::looksLikeSongsRoot(const QString &path)
{
    const QDir dir(path);
    if (!dir.exists())
        return false;
    const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        bool ok = false;
        name.toInt(&ok);
        if (ok && QFileInfo::exists(dir.filePath(name + QStringLiteral("/song.toml"))))
            return true;
    }
    return false;
}

void Library::rescan()
{
    m_entries.clear();
    const QDir root(m_root);
    if (!root.exists())
        return;

    QList<int> ids;
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const int id = name.toInt(&ok);
        if (ok)
            ids.append(id);
    }
    std::sort(ids.begin(), ids.end());

    for (const int id : ids) {
        SongEntry entry;
        entry.id = id;
        entry.directory = root.filePath(QString::number(id));
        entry.basePath = entry.directory + QStringLiteral("/song.toml");
        if (!QFileInfo::exists(entry.basePath))
            continue;

        const auto loaded = io::load(entry.basePath);
        if (!loaded) {
            entry.problem = loaded.error().parse.message.isEmpty()
                ? loaded.error().message
                : loaded.error().parse.formatted();
        } else {
            entry.readable = true;
            entry.title = loaded->title.valueOr(QString());
            entry.subtitle = loaded->subtitle.valueOr(QString());
            entry.baseLanguage = loaded->language;
            entry.active = loaded->active.valueOr(true);
        }

        const QDir dir(entry.directory);
        QStringList files = dir.entryList(QStringList { QStringLiteral("song_*.toml") },
            QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const QString code = i18n::codeFromFilename(file);
            if (code.isEmpty())
                continue;
            entry.translationLanguages.append(code);
            entry.translationPaths.append(dir.filePath(file));
        }
        m_entries.append(entry);
    }
}

const SongEntry *Library::entry(int id) const
{
    for (const SongEntry &entry : m_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

int Library::nextId() const
{
    int highest = 0;
    for (const SongEntry &entry : m_entries)
        highest = std::max(highest, entry.id);
    return highest + 1;
}

QList<SongEntry> Library::search(const QString &needle) const
{
    if (needle.trimmed().isEmpty())
        return m_entries;
    QList<SongEntry> out;
    const QString lower = needle.trimmed().toLower();
    for (const SongEntry &entry : m_entries) {
        if (entry.title.toLower().contains(lower) || entry.subtitle.toLower().contains(lower)
            || QString::number(entry.id) == lower)
            out.append(entry);
    }
    return out;
}

} // namespace ope
