// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Commands.hpp"
#include "notation/NoteParser.hpp"

namespace OpenPsalm {

EditMetadataCommand::EditMetadataCommand(SongDocument* doc, const SongData& newData, const QString& text)
    : QUndoCommand(text), m_doc(doc), m_newData(newData)
{
    m_oldData = m_doc->songData();
}

void EditMetadataCommand::undo() {
    m_doc->songData() = m_oldData;
    m_doc->revalidate();
    emit m_doc->documentModified();
}

void EditMetadataCommand::redo() {
    m_doc->songData() = m_newData;
    m_doc->revalidate();
    emit m_doc->documentModified();
}

EditPartNotesCommand::EditPartNotesCommand(SongDocument* doc, const QString& partName, const QString& newNotesText, const QString& text)
    : QUndoCommand(text), m_doc(doc), m_partName(partName), m_newNotesText(newNotesText)
{
    if (m_doc->songData().parts.contains(partName)) {
        m_oldNotesText = m_doc->songData().parts[partName].notesText;
    }
}

void EditPartNotesCommand::undo() {
    if (m_doc->songData().parts.contains(m_partName)) {
        auto& part = m_doc->songData().parts[m_partName];
        part.notesText = m_oldNotesText;
        auto parseRes = NoteParser::parse(m_oldNotesText, m_partName, m_doc->filePath());
        part.parsedMeasures = parseRes.measures;
        m_doc->revalidate();
        emit m_doc->documentModified();
    }
}

void EditPartNotesCommand::redo() {
    if (m_doc->songData().parts.contains(m_partName)) {
        auto& part = m_doc->songData().parts[m_partName];
        part.notesText = m_newNotesText;
        auto parseRes = NoteParser::parse(m_newNotesText, m_partName, m_doc->filePath());
        part.parsedMeasures = parseRes.measures;
        m_doc->revalidate();
        emit m_doc->documentModified();
    }
}

EditLyricsSectionCommand::EditLyricsSectionCommand(SongDocument* doc, const QString& sectionKey, const QString& newLyricsText, const QString& text)
    : QUndoCommand(text), m_doc(doc), m_sectionKey(sectionKey), m_newLyricsText(newLyricsText)
{
    m_oldLyricsText = m_doc->songData().lyrics.section(sectionKey);
}

void EditLyricsSectionCommand::undo() {
    m_doc->songData().lyrics.setSection(m_sectionKey, m_oldLyricsText);
    m_doc->revalidate();
    emit m_doc->documentModified();
}

void EditLyricsSectionCommand::redo() {
    m_doc->songData().lyrics.setSection(m_sectionKey, m_newLyricsText);
    m_doc->revalidate();
    emit m_doc->documentModified();
}

AddPartCommand::AddPartCommand(SongDocument* doc, const PartData& part)
    : QUndoCommand(QStringLiteral("Add Part %1").arg(part.name)), m_doc(doc), m_part(part)
{
}

void AddPartCommand::undo() {
    m_doc->songData().parts.remove(m_part.name);
    m_doc->revalidate();
    emit m_doc->documentModified();
}

void AddPartCommand::redo() {
    m_doc->songData().parts.insert(m_part.name, m_part);
    m_doc->revalidate();
    emit m_doc->documentModified();
}

RemovePartCommand::RemovePartCommand(SongDocument* doc, const QString& partName)
    : QUndoCommand(QStringLiteral("Remove Part %1").arg(partName)), m_doc(doc), m_partName(partName)
{
    if (m_doc->songData().parts.contains(partName)) {
        m_savedPart = m_doc->songData().parts[partName];
    }
}

void RemovePartCommand::undo() {
    m_doc->songData().parts.insert(m_partName, m_savedPart);
    m_doc->revalidate();
    emit m_doc->documentModified();
}

void RemovePartCommand::redo() {
    m_doc->songData().parts.remove(m_partName);
    m_doc->revalidate();
    emit m_doc->documentModified();
}

} // namespace OpenPsalm
