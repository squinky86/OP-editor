// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QUndoCommand>
#include "SongDocument.hpp"

namespace OpenPsalm {

class EditMetadataCommand : public QUndoCommand {
public:
    EditMetadataCommand(SongDocument* doc, const SongData& newData, const QString& text = QStringLiteral("Edit Metadata"));
    void undo() override;
    void redo() override;

private:
    SongDocument* m_doc;
    SongData m_oldData;
    SongData m_newData;
};

class EditPartNotesCommand : public QUndoCommand {
public:
    EditPartNotesCommand(SongDocument* doc, const QString& partName, const QString& newNotesText, const QString& text = QStringLiteral("Edit Notes"));
    void undo() override;
    void redo() override;

private:
    SongDocument* m_doc;
    QString m_partName;
    QString m_oldNotesText;
    QString m_newNotesText;
};

class EditLyricsSectionCommand : public QUndoCommand {
public:
    EditLyricsSectionCommand(SongDocument* doc, const QString& sectionKey, const QString& newLyricsText, const QString& text = QStringLiteral("Edit Lyrics"));
    void undo() override;
    void redo() override;

private:
    SongDocument* m_doc;
    QString m_sectionKey;
    QString m_oldLyricsText;
    QString m_newLyricsText;
};

class AddPartCommand : public QUndoCommand {
public:
    AddPartCommand(SongDocument* doc, const PartData& part);
    void undo() override;
    void redo() override;

private:
    SongDocument* m_doc;
    PartData m_part;
};

class RemovePartCommand : public QUndoCommand {
public:
    RemovePartCommand(SongDocument* doc, const QString& partName);
    void undo() override;
    void redo() override;

private:
    SongDocument* m_doc;
    QString m_partName;
    PartData m_savedPart;
};

} // namespace OpenPsalm
