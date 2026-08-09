// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ScoreView.hpp"
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QBrush>
#include <QPen>
#include <QFont>
#include <algorithm>

namespace OpenPsalm {

ScoreView::ScoreView(SongDocument* doc, QWidget* parent)
    : QGraphicsView(parent),
      m_doc(doc),
      m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(Qt::white);
    m_scene->setBackgroundBrush(Qt::white);
    setStyleSheet(QStringLiteral("QGraphicsView { background-color: white; }"));

    connect(m_doc, &SongDocument::documentLoaded, this, &ScoreView::refreshScore);
    connect(m_doc, &SongDocument::documentModified, this, &ScoreView::refreshScore);
}

void ScoreView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
}

void ScoreView::refreshScore() {
    m_scene->clear();
    renderStaffAndNotes();
}

void ScoreView::renderStaffAndNotes() {
    const auto& song = m_doc->effectiveData();
    QStringList partsOrder = song.partNamesInOrder();
    if (partsOrder.isEmpty()) {
        auto* txt = m_scene->addText(QStringLiteral("No parts to display."));
        txt->setDefaultTextColor(Qt::black);
        txt->setPos(20, 20);
        return;
    }

    constexpr qreal StaffLineSpacing = 10.0;
    constexpr qreal StaffHeight = 4 * StaffLineSpacing; // 5 lines
    constexpr qreal SystemSpacing = 120.0;
    constexpr qreal LeftMargin = 80.0;
    constexpr qreal MeasureWidth = 140.0;

    int maxMeasures = song.maxMeasureCount();
    if (maxMeasures == 0) maxMeasures = 1;

    qreal totalWidth = LeftMargin + maxMeasures * MeasureWidth + 40.0;
    qreal currentY = 40.0;

    // Title & Metadata header on score
    QFont titleFont(QStringLiteral("SansSerif"), 16, QFont::Bold);
    auto* titleItem = m_scene->addText(song.title, titleFont);
    titleItem->setDefaultTextColor(Qt::black);
    titleItem->setPos(LeftMargin, 5.0);

    QFont metaFont(QStringLiteral("SansSerif"), 9);
    QString metaStr = QStringLiteral("Key: %1 | Time: %2/%3 | Tempo: %4 BPM")
                      .arg(song.keySignature)
                      .arg(song.timeSigNumerator)
                      .arg(song.timeSigDenominator)
                      .arg(song.tempoBpm);
    auto* metaItem = m_scene->addText(metaStr, metaFont);
    metaItem->setDefaultTextColor(Qt::black);
    metaItem->setPos(LeftMargin, 30.0);

    currentY = 70.0;

    for (const QString& partName : partsOrder) {
        const PartData& part = song.parts[partName];

        // Part name & clef label
        QFont partFont(QStringLiteral("SansSerif"), 10, QFont::Bold);
        auto* partLabel = m_scene->addText(QStringLiteral("%1 (%2)").arg(partName).arg(clefToString(part.clef)), partFont);
        partLabel->setDefaultTextColor(Qt::black);
        partLabel->setPos(5.0, currentY - 5.0);

        // Draw 5 staff lines
        QPen staffPen(QColor(60, 60, 60), 1.0);
        for (int line = 0; line < 5; ++line) {
            qreal y = currentY + line * StaffLineSpacing;
            m_scene->addLine(LeftMargin, y, totalWidth - 40.0, y, staffPen);
        }

        // Draw barlines
        QPen barlinePen(QColor(40, 40, 40), 1.5);
        for (int m = 0; m <= maxMeasures; ++m) {
            qreal x = LeftMargin + m * MeasureWidth;
            m_scene->addLine(x, currentY, x, currentY + StaffHeight, barlinePen);

            // Measure number header
            if (m < maxMeasures) {
                QFont mNumFont(QStringLiteral("SansSerif"), 7);
                auto* mNumItem = m_scene->addText(QString::number(m + 1), mNumFont);
                mNumItem->setDefaultTextColor(QColor(120, 120, 120));
                mNumItem->setPos(x + 2.0, currentY - 18.0);
            }
        }

        // Draw notes in each measure
        for (size_t mIdx = 0; mIdx < part.parsedMeasures.size(); ++mIdx) {
            const Measure& m = part.parsedMeasures[mIdx];
            qreal mStartX = LeftMargin + mIdx * MeasureWidth;
            int eventCount = static_cast<int>(m.events.size());
            if (eventCount == 0) continue;

            qreal eventSpacing = (MeasureWidth - 20.0) / std::max(1, eventCount);

            for (int eIdx = 0; eIdx < eventCount; ++eIdx) {
                const NoteToken& ev = m.events[eIdx];
                qreal noteX = mStartX + 12.0 + eIdx * eventSpacing;

                if (ev.kind == NoteKind::Rest) {
                    // Draw rest marker
                    auto* restItem = m_scene->addRect(noteX, currentY + 1.5 * StaffLineSpacing, 8.0, 10.0, QPen(Qt::black), QBrush(Qt::black));
                    restItem->setToolTip(QStringLiteral("Rest %1").arg(ev.duration.toString()));
                } else if (ev.kind == NoteKind::Spacer) {
                    // Draw faint spacer marker
                    auto* spacer = m_scene->addRect(noteX, currentY + 1.5 * StaffLineSpacing, 6.0, 6.0, QPen(QColor(180, 180, 180), 1, Qt::DashLine), QBrush(Qt::NoBrush));
                    spacer->setToolTip(QStringLiteral("Spacer %1").arg(ev.duration.toString()));
                } else {
                    // Draw Noteheads for Note or Chord
                    for (const auto& pitch : ev.pitches) {
                        // Calculate vertical position on staff
                        // Middle C (C4) on Treble clef is 1 ledger line below bottom staff line
                        // Staff bottom line for Treble is E4 (y = currentY + 4 * StaffLineSpacing)
                        int diatonicStep = pitch.staffDiatonicStep();
                        int refDiatonicStep = (part.clef == Clef::Bass) ? 23 : 30; // D3 vs B4
                        qreal noteY = currentY + 2 * StaffLineSpacing - (diatonicStep - refDiatonicStep) * (StaffLineSpacing / 2.0);

                        QBrush noteBrush(Qt::black);
                        if (ev.duration.baseDuration <= 2) {
                            noteBrush = QBrush(Qt::white); // Open notehead for half/whole notes
                        }

                        auto* noteHead = m_scene->addEllipse(noteX - 5.0, noteY - 4.0, 10.0, 8.0, QPen(Qt::black, 1.5), noteBrush);
                        noteHead->setToolTip(QStringLiteral("%1: %2").arg(partName).arg(ev.toString()));

                        // Stem for half, quarter, eighth, etc.
                        if (ev.duration.baseDuration >= 2) {
                            m_scene->addLine(noteX + 4.5, noteY, noteX + 4.5, noteY - 26.0, QPen(Qt::black, 1.5));
                        }

                        // Connect click selection
                        noteHead->setData(0, partName);
                        noteHead->setData(1, static_cast<int>(mIdx + 1));
                        noteHead->setData(2, eIdx);
                    }
                }
            }
        }

        currentY += SystemSpacing;
    }

    m_scene->setSceneRect(0, 0, totalWidth, currentY + 40.0);
}

} // namespace OpenPsalm
