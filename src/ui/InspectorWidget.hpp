// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include "core/SongDocument.hpp"
#include "notation/NoteToken.hpp"

namespace OpenPsalm {

class InspectorWidget : public QWidget {
    Q_OBJECT

public:
    explicit InspectorWidget(SongDocument* doc, QWidget* parent = nullptr);
    void inspectNoteToken(const QString& partName, int measureIdx, int eventIdx, const NoteToken& token);
    void clearInspection();

private:
    SongDocument* m_doc{nullptr};
    QString m_currentPartName;
    int m_currentMeasureIdx{0};
    int m_currentEventIdx{0};

    QLabel* m_selectionLabel{nullptr};
    QLabel* m_rawTokenLabel{nullptr};
    QLabel* m_pitchLabel{nullptr};
    QLabel* m_durationLabel{nullptr};
    QLabel* m_flagsLabel{nullptr};
};

} // namespace OpenPsalm
