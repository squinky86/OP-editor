// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Dialogs.h"

#include "core/Validator.h"

#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace ope::ui {
namespace {

constexpr int CurrentYear = 2026;

QString arrangementLine()
{
    return QStringLiteral(
        "Arrangement by OpenPsalm, %1 and released under the CC-BY 4.0 license")
        .arg(CurrentYear);
}

QString normalizeLanguageCode(QString code)
{
    code = code.section(u' ', 0, 0).trimmed();
    QStringList pieces = code.split(u'-');
    if (pieces.isEmpty())
        return {};
    pieces[0] = pieces[0].toLower();
    for (qsizetype i = 1; i < pieces.size(); ++i) {
        if (pieces.at(i).size() == 2
            && std::all_of(pieces.at(i).begin(), pieces.at(i).end(),
                [](QChar c) { return c.isLetter(); })) {
            pieces[i] = pieces.at(i).toUpper();
        } else if (pieces.at(i).size() == 4
            && std::all_of(pieces.at(i).begin(), pieces.at(i).end(),
                [](QChar c) { return c.isLetter(); })) {
            pieces[i] = pieces.at(i).toLower();
            pieces[i][0] = pieces.at(i).front().toUpper();
        } else {
            pieces[i] = pieces.at(i).toLower();
        }
    }
    return pieces.join(u'-');
}

bool isSafeLanguageCode(const QString &code)
{
    static const QRegularExpression safe(
        QStringLiteral("^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$"));
    return safe.match(code).hasMatch();
}

QComboBox *makeDenominatorBox(QWidget *parent, int value)
{
    auto *box = new QComboBox(parent);
    for (const int denominator : ticks::TimeSignatureDenominators)
        box->addItem(QString::number(denominator), denominator);
    int index = box->findData(value);
    if (index < 0) {
        box->addItem(QObject::tr("%1 (unsupported)").arg(value), value);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
    return box;
}

} // namespace

// ------------------------------------------------------------ NewSongDialog ---

NewSongDialog::NewSongDialog(Library *library, QWidget *parent)
    : QDialog(parent), m_library(library)
{
    setWindowTitle(tr("New Song"));

    auto *layout = new QFormLayout(this);
    m_id = new QSpinBox(this);
    m_id->setRange(1, 100000);
    m_id->setValue(library->nextId());
    m_title = new QLineEdit(this);
    m_subtitle = new QLineEdit(this);
    m_key = new QComboBox(this);
    m_key->addItems(validKeySignatures());
    m_key->setCurrentText(QStringLiteral("C"));
    m_numerator = new QSpinBox(this);
    m_numerator->setRange(1, 32);
    m_numerator->setValue(4);
    m_denominator = makeDenominatorBox(this, 4);
    m_tempo = new QSpinBox(this);
    m_tempo->setRange(20, 300);
    m_tempo->setValue(100);
    m_verses = new QSpinBox(this);
    m_verses->setRange(1, 40);
    m_verses->setValue(3);
    m_measures = new QSpinBox(this);
    m_measures->setRange(1, 400);
    m_measures->setValue(8);
    m_satb = new QCheckBox(tr("Soprano, Alto, Tenor, Bass"), this);
    m_satb->setChecked(true);
    m_wordsBy = new QLineEdit(this);
    m_wordsBy->setPlaceholderText(tr("Anna B. Warner, 1860"));
    m_musicBy = new QLineEdit(this);
    m_musicBy->setPlaceholderText(tr("William B. Bradbury, 1862"));

    auto *metre = new QWidget(this);
    auto *metreLayout = new QHBoxLayout(metre);
    metreLayout->setContentsMargins(0, 0, 0, 0);
    metreLayout->addWidget(m_numerator);
    metreLayout->addWidget(new QLabel(QStringLiteral("/"), metre));
    metreLayout->addWidget(m_denominator);
    metreLayout->addStretch();

    layout->addRow(tr("Song number"), m_id);
    layout->addRow(tr("Title"), m_title);
    layout->addRow(tr("Subtitle"), m_subtitle);
    layout->addRow(tr("Key"), m_key);
    layout->addRow(tr("Metre"), metre);
    layout->addRow(tr("Tempo"), m_tempo);
    layout->addRow(tr("Verses"), m_verses);
    layout->addRow(tr("Empty measures"), m_measures);
    layout->addRow(tr("Parts"), m_satb);
    layout->addRow(tr("Words by"), m_wordsBy);
    layout->addRow(tr("Music by"), m_musicBy);

    auto *note = new QLabel(
        tr("The song starts with rests filling every measure, so it is valid from the "
           "first save. Public-domain credits are assumed; edit the copyright block if not."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #57606a;"));
    layout->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_title->setFocus();
}

QString NewSongDialog::targetPath() const
{
    return QStringLiteral("%1/%2/song.toml").arg(m_library->root()).arg(m_id->value());
}

SongDocument NewSongDialog::buildDocument() const
{
    SongDocument doc;
    doc.path = targetPath();
    doc.workId = m_id->value();
    doc.language = i18n::defaultLanguage();
    doc.title.set(m_title->text().isEmpty() ? tr("Untitled") : m_title->text());
    if (!m_subtitle->text().isEmpty())
        doc.subtitle.set(m_subtitle->text());
    doc.keySignature.set(m_key->currentText());
    doc.timeSigNumerator.set(m_numerator->value());
    doc.timeSigDenominator.set(m_denominator->currentData().toInt());
    doc.tempoBpm.set(m_tempo->value());
    doc.verseCount.set(m_verses->value());

    QStringList copyrights;
    if (!m_wordsBy->text().isEmpty())
        copyrights.append(tr("Words: %1 (public domain)").arg(m_wordsBy->text()));
    if (!m_musicBy->text().isEmpty())
        copyrights.append(tr("Music: %1 (public domain)").arg(m_musicBy->text()));
    copyrights.append(arrangementLine());
    doc.copyrights.set(copyrights);

    // Fill each measure with one rest of the whole measure's value where the
    // metre allows it, so every measure sums correctly from the start.
    const int measureTicks
        = ticks::forTimeSignature(m_numerator->value(), m_denominator->currentData().toInt());
    QStringList measures;
    for (int m = 0; m < m_measures->value(); ++m) {
        QStringList tokens;
        int remaining = measureTicks;
        const std::pair<int, int> values[7] = { { 1, ticks::Whole }, { 2, ticks::Half },
            { 4, ticks::Quarter }, { 8, ticks::Eighth }, { 16, ticks::Sixteenth },
            { 32, ticks::ThirtySecond }, { 64, ticks::SixtyFourth } };
        while (remaining > 0) {
            for (const auto &[base, value] : values) {
                if (value <= remaining) {
                    tokens.append(QStringLiteral("r%1").arg(base));
                    remaining -= value;
                    break;
                }
            }
        }
        measures.append(tokens.join(u' '));
    }
    const QString notes = measures.join(QStringLiteral(" |\n"));

    struct PartSpec {
        QString name;
        QString choralType;
        QString clef;
        int staff;
    };
    QList<PartSpec> specs;
    if (m_satb->isChecked()) {
        specs = { { QStringLiteral("Soprano"), QStringLiteral("soprano"),
                      QStringLiteral("treble"), 1 },
            { QStringLiteral("Alto"), QStringLiteral("alto"), QStringLiteral("treble"), 1 },
            { QStringLiteral("Tenor"), QStringLiteral("tenor"), QStringLiteral("bass"), 2 },
            { QStringLiteral("Bass"), QStringLiteral("bass"), QStringLiteral("bass"), 2 } };
    } else {
        specs = { { QStringLiteral("Soprano"), QStringLiteral("soprano"),
            QStringLiteral("treble"), 1 } };
    }
    for (const PartSpec &spec : specs) {
        Part part;
        part.name = spec.name;
        part.choralType.set(spec.choralType);
        part.clef.set(spec.clef);
        part.staffNumber.set(spec.staff);
        part.notes.set(notes);
        part.isNew = true;
        part.reparse();
        doc.parts.append(part);
    }

    for (int verse = 1; verse <= m_verses->value(); ++verse) {
        LyricSection section;
        section.rawText.clear();
        section.dirty = true;
        doc.lyrics.insert(QString::number(verse), section);
    }
    return doc;
}

// -------------------------------------------------------- TranslationDialog ---

TranslationDialog::TranslationDialog(
    const SongDocument &base, const QStringList &existing, QWidget *parent)
    : QDialog(parent), m_base(base), m_existing(existing)
{
    setWindowTitle(tr("Add Translation"));

    auto *layout = new QFormLayout(this);
    m_language = new QComboBox(this);
    m_language->setEditable(true);
    for (const LanguageInfo &language : i18n::languages()) {
        if (m_existing.contains(language.code))
            continue;
        m_language->addItem(
            QStringLiteral("%1 — %2").arg(language.code, language.nativeName), language.code);
    }
    m_title = new QLineEdit(this);
    m_title->setPlaceholderText(tr("Title in the translation's language"));
    m_copyVerses = new QCheckBox(
        tr("Start from the base language's verses (then translate in place)"), this);
    m_copyVerses->setChecked(true);
    m_templateCopyrights = new QCheckBox(
        tr("Pre-fill the copyright block with a translated template"), this);
    m_templateCopyrights->setChecked(true);
    m_warning = new QLabel(this);
    m_warning->setWordWrap(true);

    layout->addRow(tr("Language"), m_language);
    layout->addRow(tr("Title"), m_title);
    layout->addRow(QString(), m_copyVerses);
    layout->addRow(QString(), m_templateCopyrights);

    auto *rule = new QLabel(
        tr("A translation is an overlay: it states only what differs and inherits the tune, "
           "key, tempo, and every part from song.toml. Defining any lyric entry replaces the "
           "whole lyric map, so every verse must be present here."),
        this);
    rule->setWordWrap(true);
    rule->setStyleSheet(QStringLiteral("color: #57606a;"));
    layout->addRow(rule);
    layout->addRow(m_warning);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_language, &QComboBox::currentTextChanged, this, &TranslationDialog::updateWarning);
    updateWarning();
}

QString TranslationDialog::languageCode() const
{
    const int index = m_language->currentIndex();
    const QString selected = m_language->currentData().toString();
    if (index >= 0 && m_language->currentText() == m_language->itemText(index)
        && !selected.isEmpty())
        return selected;
    // An edited entry: take the code before any separator the user left in.
    return normalizeLanguageCode(m_language->currentText());
}

bool TranslationDialog::copyBaseVerses() const { return m_copyVerses->isChecked(); }

void TranslationDialog::updateWarning()
{
    const QString code = languageCode();
    const bool safe = isSafeLanguageCode(code);
    const bool duplicate = m_existing.contains(code, Qt::CaseInsensitive);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(safe && !duplicate);
    if (code.isEmpty()) {
        m_warning->setText(tr("Choose a language code."));
        m_warning->setStyleSheet(QStringLiteral("color: #d1242f;"));
        return;
    }
    if (!safe) {
        m_warning->setText(tr("Use a safe BCP-47 code such as es, de, pt-BR, or zh-Hant. "
                              "Spaces, dots, slashes, and underscores are not allowed."));
        m_warning->setStyleSheet(QStringLiteral("color: #d1242f;"));
        return;
    }
    if (duplicate) {
        m_warning->setText(tr("This song already has a %1 translation.").arg(code));
        m_warning->setStyleSheet(QStringLiteral("color: #d1242f;"));
        return;
    }
    if (i18n::isKnown(code)) {
        m_warning->clear();
        return;
    }
    m_warning->setText(
        tr("“%1” is not in the language registry. OpenPsalm will refuse to seed the file "
           "until this is appended to LANGUAGES in src/i18n.rs, and OPE's own bundled table "
           "needs the same entry:\n\n%2")
            .arg(code, i18n::registrySnippet(code, code, code)));
    m_warning->setStyleSheet(QStringLiteral("color: #9a6700;"));
}

SongDocument TranslationDialog::buildOverlay() const
{
    SongDocument overlay;
    const QString code = languageCode();
    overlay.isOverlay = true;
    overlay.language = code;
    overlay.workId = m_base.workId;
    overlay.path = QFileInfo(m_base.path).dir().filePath(
        QStringLiteral("song_%1.toml").arg(code));
    overlay.title.set(m_title->text().isEmpty() ? m_base.title.valueOr(QString())
                                                : m_title->text());

    if (m_templateCopyrights->isChecked()) {
        // Start from the base credits with the label words translated where we
        // know them, and a translator line where it belongs — beside the
        // original lyricist, not appended at the end.
        QStringList lines;
        for (const QString &line : m_base.copyrights.valueOr({})) {
            QString translated = line;
            if (code == QLatin1String("es")) {
                translated.replace(QStringLiteral("Words:"), QStringLiteral("Letra:"));
                translated.replace(QStringLiteral("Music:"), QStringLiteral("Música:"));
                translated.replace(QStringLiteral("Lyrics:"), QStringLiteral("Letra:"));
                translated.replace(
                    QStringLiteral("(public domain)"), QStringLiteral("(dominio público)"));
                translated.replace(QStringLiteral("Arrangement by OpenPsalm"),
                    QStringLiteral("Arreglo de OpenPsalm"));
                translated.replace(QStringLiteral("and released under the CC-BY 4.0 license"),
                    QStringLiteral("publicado bajo la licencia CC-BY 4.0"));
            }
            lines.append(translated);
            if (line.startsWith(QLatin1String("Words"))
                || line.startsWith(QLatin1String("Lyrics"))) {
                lines.append(code == QLatin1String("es")
                        ? QStringLiteral("Traducción: , (dominio público)")
                        : QStringLiteral("Translation: , (public domain)"));
            }
        }
        overlay.copyrights.set(lines);
    }

    for (auto it = m_base.lyrics.constBegin(); it != m_base.lyrics.constEnd(); ++it) {
        LyricSection section;
        section.rawText = m_copyVerses->isChecked() ? it->rawText : QString();
        section.dirty = true;
        overlay.lyrics.insert(it.key(), section);
    }
    return overlay;
}

// ----------------------------------------------------- TimeSigChangesDialog ---

TimeSigChangesDialog::TimeSigChangesDialog(
    QList<TimeSigChange> changes, int measureCount, QWidget *parent)
    : QDialog(parent), m_measureCount(std::max(1, measureCount))
{
    setWindowTitle(tr("Time Signature Changes"));
    resize(520, 320);

    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(
        tr("Each row changes the metre for <i>duration</i> measures starting at "
           "<i>measure</i>. Measures outside every row keep the song's metre."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        { tr("Measure"), tr("Numerator"), tr("Denominator"), tr("Measures") });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table, 1);

    auto *buttonRow = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add"), this);
    auto *remove = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(add);
    buttonRow->addWidget(remove);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, [this] { addRow(TimeSigChange {}); });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0)
            m_table->removeRow(row);
    });

    for (const TimeSigChange &change : changes)
        addRow(change);
}

void TimeSigChangesDialog::addRow(const TimeSigChange &change)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    const auto spin = [this](int value, int minimum, int maximum) {
        auto *box = new QSpinBox(m_table);
        box->setRange(minimum, maximum);
        box->setValue(value);
        return box;
    };
    m_table->setCellWidget(row, 0, spin(change.measure, 1, std::max(1, m_measureCount)));
    m_table->setCellWidget(row, 1, spin(change.numerator, 1, 32));
    m_table->setCellWidget(row, 2, makeDenominatorBox(m_table, change.denominator));
    m_table->setCellWidget(row, 3, spin(change.duration, 1, std::max(1, m_measureCount)));
}

QList<TimeSigChange> TimeSigChangesDialog::changes() const
{
    QList<TimeSigChange> out;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto spinValue = [this, row](int column) {
            const auto *box = qobject_cast<QSpinBox *>(m_table->cellWidget(row, column));
            return box ? box->value() : 1;
        };
        const auto *denominator
            = qobject_cast<QComboBox *>(m_table->cellWidget(row, 2));
        out.append(TimeSigChange { spinValue(0), spinValue(1),
            denominator ? denominator->currentData().toInt() : 4, spinValue(3) });
    }
    return out;
}

// -------------------------------------------------------- PreferencesDialog ---

PreferencesDialog::PreferencesDialog(QString songsRoot, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    auto *layout = new QFormLayout(this);

    auto *rootRow = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(rootRow);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    m_root = new QLineEdit(songsRoot, rootRow);
    m_root->setMinimumWidth(360);
    auto *browse = new QPushButton(tr("Browse…"), rootRow);
    rootLayout->addWidget(m_root);
    rootLayout->addWidget(browse);
    layout->addRow(tr("Songs directory"), rootRow);

    auto *note = new QLabel(
        tr("The OP-songs checkout: the directory holding the numbered song folders. It is the "
           "only thing OPE needs on disk."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #57606a;"));
    layout->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(this,
            tr("Select the songs directory"), m_root->text());
        if (!chosen.isEmpty())
            m_root->setText(chosen);
    });
}

QString PreferencesDialog::songsRoot() const { return m_root->text(); }

// ------------------------------------------------------------------ errors ---

void showParseError(QWidget *parent, const LoadError &error)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(QObject::tr("Cannot open this file"));

    if (!error.message.isEmpty()) {
        box.setText(QObject::tr("%1\n\n%2").arg(error.path, error.message));
        box.exec();
        return;
    }

    box.setText(QObject::tr("%1\n\nLine %2, column %3: %4")
                    .arg(error.path)
                    .arg(error.parse.line)
                    .arg(error.parse.column)
                    .arg(error.parse.message));
    // The offending line with a caret under the column, so the fix is obvious
    // in the text editor the user will reach for.
    const QString caret = QString(std::max(0, error.parse.column - 1), u' ') + u'^';
    box.setDetailedText(QStringLiteral("%1\n%2").arg(error.parse.sourceLine, caret));
    box.setInformativeText(QObject::tr(
        "The file was not opened. A song whose TOML does not parse cannot be edited safely — "
        "fix the syntax in a text editor and try again."));

    auto *copy = box.addButton(QObject::tr("Copy error"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.setDefaultButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == copy) {
        // Deliberately after exec(): copying is the action, not a dismissal.
        QGuiApplication::clipboard()->setText(
            QStringLiteral("%1:%2:%3: %4\n%5")
                .arg(error.path)
                .arg(error.parse.line)
                .arg(error.parse.column)
                .arg(error.parse.message, error.parse.sourceLine));
    }
}

void showAbout(QWidget *parent)
{
    QMessageBox::about(parent, QObject::tr("About OpenPsalm Editor"),
        QObject::tr("<h3>OpenPsalm Editor %1</h3>"
                    "<p>An editor for OpenPsalm <code>song.toml</code> files.</p>"
                    "<p>Copyright © 2026 Jon Hood, OpenPsalm.com</p>"
                    "<p>This program is free software: you may redistribute it and/or modify it "
                    "under the terms of the GNU Affero General Public License as published by "
                    "the Free Software Foundation, either version 3 of the License, or (at your "
                    "option) any later version. It is distributed in the hope that it will be "
                    "useful, but WITHOUT ANY WARRANTY; without even the implied warranty of "
                    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero "
                    "General Public License for more details.</p>")
            .arg(QStringLiteral(OPE_VERSION)));
}

} // namespace ope::ui
