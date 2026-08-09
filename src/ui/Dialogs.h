// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Opening a song, starting a new one, adding a translation, preferences, and
// the dialog that refuses to open a file whose TOML does not parse.

#pragma once

#include "core/Library.h"
#include "core/Song.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;
QT_END_NAMESPACE

namespace ope::ui {

/// The searchable list of songs in the configured songs directory.
class SongBrowser : public QDialog {
    Q_OBJECT
public:
    explicit SongBrowser(Library *library, QWidget *parent = nullptr);
    [[nodiscard]] QString chosenPath() const { return m_chosenPath; }

private:
    void repopulate();

    Library *m_library = nullptr;
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QString m_chosenPath;
};

/// Collects what a new song needs to be seedable, and nothing more.
class NewSongDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewSongDialog(Library *library, QWidget *parent = nullptr);

    [[nodiscard]] SongDocument buildDocument() const;
    [[nodiscard]] QString targetPath() const;

private:
    Library *m_library = nullptr;
    QSpinBox *m_id = nullptr;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_subtitle = nullptr;
    QComboBox *m_key = nullptr;
    QSpinBox *m_numerator = nullptr;
    QSpinBox *m_denominator = nullptr;
    QSpinBox *m_tempo = nullptr;
    QSpinBox *m_verses = nullptr;
    QSpinBox *m_measures = nullptr;
    QCheckBox *m_satb = nullptr;
    QLineEdit *m_wordsBy = nullptr;
    QLineEdit *m_musicBy = nullptr;
};

/// Picks a language for a new `song_{lang}.toml`, and says plainly that the
/// code must also be registered in OpenPsalm's src/i18n.rs.
class TranslationDialog : public QDialog {
    Q_OBJECT
public:
    TranslationDialog(const SongDocument &base, const QStringList &existing,
        QWidget *parent = nullptr);

    [[nodiscard]] QString languageCode() const;
    [[nodiscard]] bool copyBaseVerses() const;
    [[nodiscard]] SongDocument buildOverlay() const;

private:
    void updateWarning();

    const SongDocument &m_base;
    QStringList m_existing;
    QComboBox *m_language = nullptr;
    QLineEdit *m_title = nullptr;
    QCheckBox *m_copyVerses = nullptr;
    QCheckBox *m_templateCopyrights = nullptr;
    QLabel *m_warning = nullptr;
};

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QString songsRoot, QWidget *parent = nullptr);
    [[nodiscard]] QString songsRoot() const;

private:
    QLineEdit *m_root = nullptr;
};

/// Shows a TOML syntax error with the offending line and a caret. A file that
/// does not parse is not opened at all.
void showParseError(QWidget *parent, const LoadError &error);

void showAbout(QWidget *parent);

} // namespace ope::ui
