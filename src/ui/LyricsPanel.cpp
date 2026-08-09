// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "LyricsPanel.h"

#include <QComboBox>
#include <QRegularExpression>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace ope::ui {
namespace {

/// U+203F. Two words sung on one note are joined with it; a lyric slot cannot
/// contain a space.
const QString Undertie = QString(QChar(0x203F));

QColor colorMelisma() { return QColor(0xf0, 0xf0, 0xf0); }
QColor colorMismatch() { return QColor(0xff, 0xe3, 0xe3); }
QColor colorPlaceholder() { return QColor(0xff, 0xf6, 0xd9); }

QString sectionLabel(const AttachedSection &section)
{
    if (section.isChorus)
        return QObject::tr("chorus");
    if (section.isCoda)
        return QObject::tr("coda");
    return QObject::tr("verse %1").arg(section.verseNumber);
}

} // namespace

LyricsPanel::LyricsPanel(Session *session, QWidget *parent) : QWidget(parent), m_session(session)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Part"), this));
    m_partSelector = new QComboBox(this);
    top->addWidget(m_partSelector);
    m_undertie = new QToolButton(this);
    m_undertie->setText(Undertie);
    m_undertie->setToolTip(tr("Insert an undertie (U+203F): two words sung on one note"));
    top->addWidget(m_undertie);
    top->addStretch();
    layout->addLayout(top);

    m_tabs = new QTabWidget(this);

    m_textPage = new QWidget(m_tabs);
    auto *textLayout = new QVBoxLayout(m_textPage);
    auto *scroll = new QScrollArea(m_textPage);
    scroll->setWidgetResizable(true);
    m_editorHost = new QWidget(scroll);
    new QVBoxLayout(m_editorHost);
    scroll->setWidget(m_editorHost);
    textLayout->addWidget(scroll);
    m_tabs->addTab(m_textPage, tr("Text"));

    m_gridPage = new QWidget(m_tabs);
    auto *gridLayout = new QVBoxLayout(m_gridPage);
    m_gridHint = new QLabel(m_gridPage);
    m_gridHint->setWordWrap(true);
    m_gridHint->setStyleSheet(QStringLiteral("color: #57606a;"));
    m_grid = new QTableWidget(m_gridPage);
    m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_grid->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    gridLayout->addWidget(m_gridHint);
    gridLayout->addWidget(m_grid);
    m_tabs->addTab(m_gridPage, tr("Alignment"));

    layout->addWidget(m_tabs);

    connect(m_partSelector, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_loading)
            refresh();
    });
    connect(m_undertie, &QToolButton::clicked, this, &LyricsPanel::insertUndertie);
    connect(m_grid, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (!m_loading)
            commitCell(row, column);
    });
    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(600);
    connect(&m_commitTimer, &QTimer::timeout, this, [this] {
        const QHash<QString, QString> pending = std::exchange(m_pendingCommits, {});
        for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
            commitText(it.key(), it.value());
    });

    connect(session, &Session::documentChanged, this, &LyricsPanel::refresh);
    connect(session, &Session::languageChanged, this, &LyricsPanel::refresh);
    refresh();
}

QString LyricsPanel::currentPartName() const { return m_partSelector->currentData().toString(); }

void LyricsPanel::refresh()
{
    m_loading = true;

    const SongDocument &doc = m_session->effectiveDocument();
    const QString previous = currentPartName();
    m_partSelector->clear();
    for (const Part *part : doc.partsInDisplayOrder()) {
        const QString suffix = part->lyrics.isEmpty() ? QString() : tr("  (own lyrics)");
        m_partSelector->addItem(part->name + suffix, part->name);
    }
    const int index = m_partSelector->findData(previous);
    m_partSelector->setCurrentIndex(index >= 0 ? index : 0);

    rebuildTextEditors();
    rebuildGrid();
    m_loading = false;
}

void LyricsPanel::rebuildTextEditors()
{
    auto *layout = qobject_cast<QVBoxLayout *>(m_editorHost->layout());
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_editors.clear();

    if (!m_session->isOpen())
        return;

    const SongDocument &doc = m_session->effectiveDocument();
    const QString partName = currentPartName();
    const Part *part = doc.part(partName);
    const PartAlignment &alignment = m_session->alignment(partName);

    // Every key the part actually sings, plus any shared sections it splices.
    QStringList keys = doc.verseKeys();
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
        if (SongDocument::isChorusKey(it.key()) || SongDocument::isCodaKey(it.key()))
            keys.append(it.key());
    }
    keys.append(doc.sharedKeys());
    if (part) {
        for (auto it = part->lyrics.constBegin(); it != part->lyrics.constEnd(); ++it) {
            if (!keys.contains(it.key()))
                keys.append(it.key());
        }
    }

    for (const QString &key : keys) {
        const bool perPart = part && part->lyrics.contains(key);
        const QString title = perPart
            ? tr("lyrics.%1  (this part's own text)").arg(key)
            : tr("lyrics.%1").arg(key);
        auto *box = new QGroupBox(title, m_editorHost);
        auto *boxLayout = new QVBoxLayout(box);

        auto *editor = new QPlainTextEdit(box);
        const QString text = perPart ? part->lyrics.value(key).rawText
                                     : doc.lyrics.value(key).rawText;
        editor->setPlainText(text);
        editor->setMaximumHeight(86);
        editor->setPlaceholderText(tr("Je -- sus loves me! This I know,"));
        boxLayout->addWidget(editor);

        auto *counter = new QLabel(box);
        counter->setStyleSheet(QStringLiteral("color: #57606a;"));
        boxLayout->addWidget(counter);

        const auto updateCounter = [this, counter, editor, key, &alignment] {
            const QStringList syllables
                = LyricSection { editor->toPlainText(), {}, false }.syllables();
            // What this section must come to: a verse must match the longest
            // verse of the part (that is what the seeder lays out against), and
            // a chorus or coda gets whatever slots remain after its offset.
            int required = -1;
            QString requiredLabel;
            for (const AttachedSection &section : alignment.sections) {
                if (section.key != key)
                    continue;
                if (section.isChorus || section.isCoda) {
                    required = static_cast<int>(alignment.lyricSlots.size()) - section.slotOffset;
                    requiredLabel = tr("slots from %1").arg(section.slotOffset);
                } else {
                    required = alignment.maxVerseLength;
                    requiredLabel = tr("slots (the longest verse of this part)");
                }
            }
            if (SongDocument::isSharedKey(key)) {
                counter->setText(tr("%n syllable(s) — spliced wherever @%1 appears",
                                     nullptr, static_cast<int>(syllables.size()))
                                     .arg(key));
                counter->setStyleSheet(QStringLiteral("color: #57606a;"));
                return;
            }
            if (required < 0) {
                counter->setText(tr("%n syllable(s)", nullptr, static_cast<int>(syllables.size())));
                return;
            }
            const bool fits = syllables.size() == required;
            counter->setText(tr("%1 syllables / %2 %3")
                                 .arg(syllables.size())
                                 .arg(required)
                                 .arg(requiredLabel));
            counter->setStyleSheet(fits ? QStringLiteral("color: #1a7f37;")
                                        : QStringLiteral("color: #d1242f; font-weight: bold;"));
        };
        updateCounter();
        connect(editor, &QPlainTextEdit::textChanged, this, updateCounter);
        // Commit on a pause rather than per keystroke, so the undo stack holds
        // edits and not letters.
        connect(editor, &QPlainTextEdit::textChanged, this, [this, editor, key] {
            if (m_loading)
                return;
            m_pendingCommits.insert(key, editor->toPlainText());
            m_commitTimer.start();
        });

        m_editors.append({ key, editor });
        layout->addWidget(box);
    }
    layout->addStretch();
}

void LyricsPanel::rebuildGrid()
{
    m_grid->clear();
    if (!m_session->isOpen())
        return;

    const QString partName = currentPartName();
    const PartAlignment &alignment = m_session->alignment(partName);
    const SongDocument &doc = m_session->effectiveDocument();
    const Part *part = doc.part(partName);
    if (!part)
        return;

    const int columns = static_cast<int>(alignment.lyricSlots.size());
    const int rows = static_cast<int>(alignment.sections.size()) + 1;  // +1 for the note row
    m_grid->setColumnCount(columns);
    m_grid->setRowCount(rows);

    QStringList headers;
    for (int slot = 0; slot < columns; ++slot) {
        const Slot &position = alignment.lyricSlots.at(slot);
        headers.append(tr("m%1\n#%2").arg(position.measureIndex + 1).arg(slot));
    }
    m_grid->setHorizontalHeaderLabels(headers);

    QStringList rowLabels { tr("note") };
    for (const AttachedSection &section : alignment.sections)
        rowLabels.append(sectionLabel(section));
    m_grid->setVerticalHeaderLabels(rowLabels);

    // Row 0: the note each slot sits on, so a translator can see the rhythm.
    for (int slot = 0; slot < columns; ++slot) {
        const Slot &position = alignment.lyricSlots.at(slot);
        QString text;
        if (position.measureIndex < part->stream.measureCount()) {
            const Measure &measure = part->stream.measures().at(position.measureIndex);
            if (position.eventIndex < measure.events.size()) {
                const Event &event = measure.events.at(position.eventIndex);
                text = event.pitches.isEmpty()
                    ? QStringLiteral("—")
                    : QStringLiteral("%1%2/%3")
                          .arg(event.pitches.first().step)
                          .arg(event.pitches.first().octave)
                          .arg(event.duration.base);
            }
        }
        auto *item = new QTableWidgetItem(text);
        item->setFlags(Qt::ItemIsEnabled);
        item->setForeground(QColor(0x57, 0x60, 0x6a));
        if (position.dashedContinuation)
            item->setBackground(colorPlaceholder());
        m_grid->setItem(0, slot, item);
    }

    for (int row = 0; row < alignment.sections.size(); ++row) {
        const AttachedSection &section = alignment.sections.at(row);
        for (int slot = 0; slot < columns; ++slot) {
            const bool inSection = slot >= section.slotOffset
                && slot < section.slotOffset + section.syllables.size();
            auto *item = new QTableWidgetItem(
                inSection ? alignment.syllableAt(section, slot) : QString());
            if (!inSection) {
                item->setFlags(Qt::ItemIsEnabled);
                item->setBackground(colorMelisma());
            } else {
                item->setData(Qt::UserRole, section.key);
                item->setData(Qt::UserRole + 1, slot - section.slotOffset);
                if (item->text() == QLatin1String("_"))
                    item->setBackground(colorPlaceholder());
            }
            m_grid->setItem(row + 1, slot, item);
        }
        // Overflow: syllables with no slot to land on.
        const int overflow
            = section.slotOffset + static_cast<int>(section.syllables.size()) - columns;
        if (overflow > 0) {
            for (int i = 0; i < columns && i < 1; ++i) {
                auto *item = m_grid->item(row + 1, columns - 1);
                if (item)
                    item->setBackground(colorMismatch());
            }
        }
    }

    QStringList hints;
    hints.append(tr("%1 lyric slots").arg(columns));
    if (alignment.chorusFirst)
        hints.append(tr("refrain-first: the chorus starts at slot 0, so verses follow it"));
    if (!alignment.errors.isEmpty())
        hints.append(alignment.errors.join(QStringLiteral("; ")));
    hints.append(tr("Grey cells take no syllable — a melisma continuation, or outside this "
                    "section's range."));
    m_gridHint->setText(hints.join(QStringLiteral("  ·  ")));
}

void LyricsPanel::commitCell(int row, int column)
{
    if (row < 1)
        return;
    QTableWidgetItem *item = m_grid->item(row, column);
    if (!item)
        return;
    const QString key = item->data(Qt::UserRole).toString();
    const int indexInSection = item->data(Qt::UserRole + 1).toInt();
    if (key.isEmpty())
        return;

    const QString partName = currentPartName();
    const PartAlignment &alignment = m_session->alignment(partName);
    const auto section = std::find_if(alignment.sections.begin(), alignment.sections.end(),
        [&key](const AttachedSection &candidate) { return candidate.key == key; });
    if (section == alignment.sections.end())
        return;

    // Rebuild the raw text from its syllables, keeping the ` -- ` structure the
    // author wrote everywhere the syllable itself did not change.
    const SongDocument &doc = m_session->effectiveDocument();
    const Part *part = doc.part(partName);
    const bool perPart = part && part->lyrics.contains(key);
    const QString original = perPart ? part->lyrics.value(key).rawText
                                     : doc.lyrics.value(key).rawText;

    QStringList tokens = original.split(QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);
    int syllableIndex = 0;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i) == QLatin1String("--"))
            continue;
        if (syllableIndex == indexInSection) {
            tokens[i] = item->text().trimmed();
            break;
        }
        ++syllableIndex;
    }
    const QString updated = tokens.join(u' ');
    commitText(key, updated);
}

void LyricsPanel::commitText(const QString &key, const QString &text)
{
    if (!m_session->isOpen())
        return;
    const QString partName = currentPartName();
    const SongDocument &doc = m_session->effectiveDocument();
    const Part *part = doc.part(partName);
    const bool perPart = part && part->lyrics.contains(key);

    const QString existing = perPart ? part->lyrics.value(key).rawText
                                     : doc.lyrics.value(key).rawText;
    if (existing == text)
        return;

    m_session->mutate(tr("Edit lyrics"), [&](SongDocument &document) {
        if (perPart) {
            if (Part *target = document.part(partName)) {
                LyricSection &section = target->lyrics[key];
                section.rawText = text;
                section.dirty = true;
            }
            return;
        }
        LyricSection &section = document.lyrics[key];
        section.rawText = text;
        section.dirty = true;
    });
    Q_EMIT statusMessage(tr("lyrics.%1 updated").arg(key));
}

void LyricsPanel::focusSlot(const QString &partName, int slot)
{
    const int index = m_partSelector->findData(partName);
    if (index >= 0)
        m_partSelector->setCurrentIndex(index);
    m_tabs->setCurrentWidget(m_gridPage);
    if (slot >= 0 && slot < m_grid->columnCount() && m_grid->rowCount() > 1) {
        m_grid->setCurrentCell(1, slot);
        m_grid->scrollToItem(m_grid->item(1, slot));
    }
}

void LyricsPanel::insertUndertie()
{
    for (const auto &[key, editor] : m_editors) {
        if (editor->hasFocus()) {
            editor->insertPlainText(Undertie);
            return;
        }
    }
    if (QTableWidgetItem *item = m_grid->currentItem()) {
        m_grid->editItem(item);
        item->setText(item->text() + Undertie);
    }
}

} // namespace ope::ui
