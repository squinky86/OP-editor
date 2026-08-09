// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "NotesEditor.hpp"
#include "core/Commands.hpp"
#include "notation/NoteParser.hpp"
#include "notation/NoteSerializer.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFontDatabase>

namespace OpenPsalm {

NotesEditor::NotesEditor(SongDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc)
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* topLayout = new QHBoxLayout();
    auto* formatBtn = new QPushButton(QStringLiteral("Format Measures (4/line)"), this);
    topLayout->addStretch();
    topLayout->addWidget(formatBtn);
    mainLayout->addLayout(topLayout);

    m_partTabs = new QTabWidget(this);
    mainLayout->addWidget(m_partTabs);

    connect(formatBtn, &QPushButton::clicked, this, &NotesEditor::formatNotes);
    connect(m_doc, &SongDocument::documentLoaded, this, &NotesEditor::refreshFromDocument);
}

void NotesEditor::refreshFromDocument() {
    m_updating = true;
    m_partTabs->clear();
    m_editors.clear();

    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(11);

    const auto& song = m_doc->songData();
    QStringList orderedNames = song.partNamesInOrder();

    for (const QString& name : orderedNames) {
        auto* editor = new QPlainTextEdit(this);
        editor->setFont(monoFont);
        editor->setPlainText(song.parts[name].notesText.trimmed());

        connect(editor, &QPlainTextEdit::textChanged, this, &NotesEditor::onNotesChanged);

        m_editors.insert(name, editor);
        m_partTabs->addTab(editor, name);
    }

    m_updating = false;
}

void NotesEditor::onNotesChanged() {
    if (m_updating) return;

    int currentIdx = m_partTabs->currentIndex();
    if (currentIdx < 0) return;

    QString partName = m_partTabs->tabText(currentIdx);
    if (!m_editors.contains(partName)) return;

    QString text = m_editors[partName]->toPlainText();
    m_doc->undoStack()->push(new EditPartNotesCommand(m_doc, partName, text));
}

void NotesEditor::formatNotes() {
    int currentIdx = m_partTabs->currentIndex();
    if (currentIdx < 0) return;

    QString partName = m_partTabs->tabText(currentIdx);
    if (!m_editors.contains(partName)) return;

    QString text = m_editors[partName]->toPlainText();
    auto parseRes = NoteParser::parse(text, partName, m_doc->filePath());
    if (!parseRes.hasErrors) {
        QString formatted = NoteSerializer::serialize(parseRes.measures, 4);
        m_updating = true;
        m_editors[partName]->setPlainText(formatted);
        m_updating = false;
        m_doc->undoStack()->push(new EditPartNotesCommand(m_doc, partName, formatted));
    }
}

} // namespace OpenPsalm
