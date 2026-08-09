// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The songs directory, and the languages a song may be authored in.
//
// The language registry is bundled rather than read from an OpenPsalm checkout:
// the only thing OPE needs on disk is a songs directory. Adding a language is
// therefore a two-repo change, and the dialog that offers languages says so.

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace ope {

/// A language song content may be authored in. Mirrors LANGUAGES in
/// OpenPsalm's src/i18n.rs. APPEND ONLY: an ordinal is part of the database
/// identity of every translation row and must never be reused or renumbered.
struct LanguageInfo {
    QString code;         ///< BCP-47, and the song_{code}.toml suffix
    int ordinal = 0;      ///< permanent and unique
    QString englishName;
    QString nativeName;   ///< endonym, shown in pickers
};

namespace i18n {

inline constexpr int IdStride = 1'000'000;
[[nodiscard]] QString defaultLanguage();
[[nodiscard]] const QList<LanguageInfo> &languages();
[[nodiscard]] bool isKnown(QStringView code);
[[nodiscard]] const LanguageInfo *lookup(QStringView code);
/// Language code of a `song_{lang}.toml` filename, or empty when it is not one.
[[nodiscard]] QString codeFromFilename(QStringView filename);
[[nodiscard]] int baseRowId(int workId);
/// Database row id a translation would receive, for display only.
[[nodiscard]] int translationRowId(int workId, QStringView code);
/// The Rust snippet to append to src/i18n.rs for an unregistered code.
[[nodiscard]] QString registrySnippet(QStringView code, QStringView englishName,
    QStringView nativeName);

} // namespace i18n

/// One song directory: songs/{id}/, with the files it holds.
struct SongEntry {
    int id = 0;
    QString directory;
    QString basePath;         ///< songs/{id}/song.toml
    QString title;
    QString subtitle;
    QString baseLanguage;
    QStringList translationLanguages;
    QStringList translationPaths;
    bool active = true;
    bool readable = false;
    QString problem;          ///< set when the base file could not be read

    [[nodiscard]] QString displayTitle() const;
    [[nodiscard]] QStringList allLanguages() const;
    [[nodiscard]] QString pathForLanguage(QStringView code) const;
};

/// Scans a songs directory. Reads only the header of each file, so opening the
/// browser over 200 songs stays instant.
class Library {
public:
    void setRoot(QString root) { m_root = std::move(root); }
    [[nodiscard]] const QString &root() const noexcept { return m_root; }

    /// True when `root` looks like an OP-songs checkout (has numeric song dirs).
    [[nodiscard]] static bool looksLikeSongsRoot(const QString &path);

    void rescan();
    [[nodiscard]] const QList<SongEntry> &entries() const noexcept { return m_entries; }
    [[nodiscard]] const SongEntry *entry(int id) const;
    /// One past the highest existing song id — the number a new song takes.
    [[nodiscard]] int nextId() const;
    [[nodiscard]] QList<SongEntry> search(const QString &needle) const;

private:
    QString m_root;
    QList<SongEntry> m_entries;
};

} // namespace ope
