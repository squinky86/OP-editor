// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "InspectorWidget.hpp"
#include <QFormLayout>
#include <QGroupBox>

namespace OpenPsalm {

InspectorWidget::InspectorWidget(SongDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc)
{
    auto* layout = new QVBoxLayout(this);
    auto* groupBox = new QGroupBox(QStringLiteral("Selected Element"), this);
    auto* form = new QFormLayout(groupBox);

    m_selectionLabel = new QLabel(QStringLiteral("None"), groupBox);
    m_rawTokenLabel = new QLabel(QStringLiteral("-"), groupBox);
    m_pitchLabel = new QLabel(QStringLiteral("-"), groupBox);
    m_durationLabel = new QLabel(QStringLiteral("-"), groupBox);
    m_flagsLabel = new QLabel(QStringLiteral("-"), groupBox);

    form->addRow(QStringLiteral("Element:"), m_selectionLabel);
    form->addRow(QStringLiteral("Token:"), m_rawTokenLabel);
    form->addRow(QStringLiteral("Pitch/Kind:"), m_pitchLabel);
    form->addRow(QStringLiteral("Duration:"), m_durationLabel);
    form->addRow(QStringLiteral("Modifiers:"), m_flagsLabel);

    layout->addWidget(groupBox);
    layout->addStretch();
}

void InspectorWidget::inspectNoteToken(const QString& partName, int measureIdx, int eventIdx, const NoteToken& token) {
    m_currentPartName = partName;
    m_currentMeasureIdx = measureIdx;
    m_currentEventIdx = eventIdx;

    m_selectionLabel->setText(QStringLiteral("%1 (m.%2, ev.%3)").arg(partName).arg(measureIdx).arg(eventIdx + 1));
    m_rawTokenLabel->setText(token.toString());

    if (token.kind == NoteKind::Rest) {
        m_pitchLabel->setText(QStringLiteral("Rest (r)"));
    } else if (token.kind == NoteKind::Spacer) {
        m_pitchLabel->setText(QStringLiteral("Spacer (s)"));
    } else if (token.kind == NoteKind::Chord) {
        QStringList pList;
        for (const auto& p : token.pitches) pList.append(p.toString());
        m_pitchLabel->setText(QStringLiteral("Chord <%1>").arg(pList.join(QLatin1Char(' '))));
    } else if (!token.pitches.empty()) {
        m_pitchLabel->setText(QStringLiteral("Note (%1, MIDI %2)").arg(token.pitches[0].toString()).arg(token.pitches[0].midiNumber()));
    }

    m_durationLabel->setText(QStringLiteral("%1 (%2 ticks)").arg(token.duration.toString()).arg(token.duration.toInternalTicks()));

    QStringList flags;
    if (token.slurStart) flags.append(QStringLiteral("SlurStart"));
    if (token.slurEnd) flags.append(QStringLiteral("SlurEnd"));
    if (token.beamStart) flags.append(QStringLiteral("BeamStart"));
    if (token.beamEnd) flags.append(QStringLiteral("BeamEnd"));
    if (token.tie) flags.append(QStringLiteral("Tie"));
    if (token.fermata) flags.append(QStringLiteral("Fermata"));
    if (token.staccato) flags.append(QStringLiteral("Staccato"));
    if (token.sectionMarker == SectionMarker::Chorus) flags.append(QStringLiteral("Chorus(@c)"));
    if (token.sectionMarker == SectionMarker::Coda) flags.append(QStringLiteral("Coda(@e)"));
    if (token.sharedSectionIndex > 0) flags.append(QStringLiteral("@s%1").arg(token.sharedSectionIndex));

    m_flagsLabel->setText(flags.isEmpty() ? QStringLiteral("None") : flags.join(QLatin1String(", ")));
}

void InspectorWidget::clearInspection() {
    m_currentPartName.clear();
    m_currentMeasureIdx = 0;
    m_currentEventIdx = 0;

    m_selectionLabel->setText(QStringLiteral("None"));
    m_rawTokenLabel->setText(QStringLiteral("-"));
    m_pitchLabel->setText(QStringLiteral("-"));
    m_durationLabel->setText(QStringLiteral("-"));
    m_flagsLabel->setText(QStringLiteral("-"));
}

} // namespace OpenPsalm
