// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include "core/SongDocument.hpp"
#include "notation/NoteToken.hpp"

namespace OpenPsalm {

class ScoreView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ScoreView(SongDocument* doc, QWidget* parent = nullptr);
    void refreshScore();

signals:
    void noteSelected(const QString& partName, int measureIdx, int eventIdx, const NoteToken& token);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void renderStaffAndNotes();

    SongDocument* m_doc{nullptr};
    QGraphicsScene* m_scene{nullptr};
};

} // namespace OpenPsalm
