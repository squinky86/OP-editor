// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QPlainTextEdit>
#include "core/SongDocument.hpp"

namespace OpenPsalm {

class NotesEditor : public QWidget {
    Q_OBJECT

public:
    explicit NotesEditor(SongDocument* doc, QWidget* parent = nullptr);
    void refreshFromDocument();

private slots:
    void onNotesChanged();
    void formatNotes();

private:
    SongDocument* m_doc{nullptr};
    bool m_updating{false};

    QTabWidget* m_partTabs{nullptr};
    QMap<QString, QPlainTextEdit*> m_editors;
};

} // namespace OpenPsalm
