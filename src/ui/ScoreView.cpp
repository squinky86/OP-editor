// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "ScoreView.h"

#include "Glyphs.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ope::ui {
namespace {

constexpr qreal StaffGapSpaces = 8.0;      ///< vertical gap between staves
constexpr qreal SystemGapSpaces = 5.0;
constexpr qreal LyricRowSpaces = 2.2;
constexpr qreal RulerSpaces = 4.2;
constexpr qreal LeftMarginSpaces = 8.0;
constexpr qreal RightMarginSpaces = 2.0;
constexpr qreal TopMarginSpaces = 3.0;

QColor colorText() { return QColor(0x1a, 0x1a, 0x1a); }
QColor colorStaff() { return QColor(0x44, 0x44, 0x44); }
QColor colorSpacer() { return QColor(0x99, 0x99, 0x99); }
QColor colorSelection() { return QColor(0x1f, 0x6f, 0xeb); }
QColor colorSelectionFill() { return QColor(0x1f, 0x6f, 0xeb, 42); }
QColor colorPlayback() { return QColor(0x18, 0x94, 0x4e); }
QColor colorProblem() { return QColor(0xd1, 0x24, 0x2f); }
QColor colorInherited() { return QColor(0x88, 0x88, 0x88); }

/// Diatonic index of the bottom staff line for each clef.
int bottomLineDiatonic(const QString &clef)
{
    if (clef == QLatin1String("bass"))
        return 2 * 7 + 4;  // G2
    return 4 * 7 + 2;      // E4, for treble and treble_8
}

int flagCountFor(int base)
{
    switch (base) {
    case 8: return 1;
    case 16: return 2;
    case 32: return 3;
    case 64: return 4;
    default: return 0;
    }
}

} // namespace

ScoreView::ScoreView(Session *session, QWidget *parent)
    : QAbstractScrollArea(parent), m_session(session)
{
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(true);
    QPalette palette = viewport()->palette();
    palette.setColor(QPalette::Window, Qt::white);
    viewport()->setPalette(palette);

    connect(session, &Session::documentChanged, this, &ScoreView::relayout);
    connect(session, &Session::selectionChanged, this, [this] { viewport()->update(); });
}

void ScoreView::setPhrasedLayout(bool phrased)
{
    m_phrased = phrased;
    relayout();
}

void ScoreView::setActiveVerse(int verse)
{
    const int wanted = std::max(1, verse);
    if (wanted == m_verse)
        return;
    m_verse = wanted;
    // Which verse is showing changes both the row count and how wide the
    // measures have to be to fit its syllables, so this is a relayout.
    relayout();
}

void ScoreView::setShowAllVerses(bool all)
{
    if (all == m_allVerses)
        return;
    m_allVerses = all;
    relayout();
}

bool ScoreView::showsSection(const AttachedSection &section) const
{
    return m_allVerses || section.isChorus || section.isCoda || section.verseNumber == m_verse;
}

QFont ScoreView::lyricFont() const
{
    QFont f = font();
    f.setPointSizeF(m_staffSpace * 1.45);
    f.setItalic(false);
    f.setBold(false);
    return f;
}

MeasureGrid ScoreView::buildGrid(int measureIndex) const
{
    const SongDocument &doc = m_session->effectiveDocument();
    const qreal space = m_staffSpace;
    const QFontMetricsF metrics(lyricFont());
    const qreal syllableGap = 0.8 * space;

    // 1. Every tick at which any voice starts a note, plus the barline.
    QSet<int> tickSet { 0 };
    int measureTicks = 0;
    for (const Part &part : doc.parts) {
        if (measureIndex >= part.stream.measureCount())
            continue;
        const Measure &measure = part.stream.measures().at(measureIndex);
        int tick = 0;
        for (const Event &event : measure.events) {
            tickSet.insert(tick);
            tick += event.playedTicks();
        }
        measureTicks = std::max(measureTicks, tick);
    }
    if (measureTicks <= 0)
        measureTicks = std::max(1, doc.expectedTicksForMeasure(measureIndex + 1));

    // 2. The widest syllable landing on each of those ticks, over shown verses.
    QHash<int, qreal> syllableWidth;
    for (const Part &part : doc.parts) {
        const PartAlignment &alignment = m_session->alignment(part.name);
        for (int slot = 0; slot < alignment.lyricSlots.size(); ++slot) {
            const Slot &position = alignment.lyricSlots.at(slot);
            if (position.measureIndex != measureIndex)
                continue;
            qreal widest = 0;
            for (const AttachedSection &section : alignment.sections) {
                if (!showsSection(section))
                    continue;
                const QString syllable = alignment.syllableAt(section, slot);
                if (!syllable.isEmpty())
                    widest = std::max(widest, metrics.horizontalAdvance(syllable));
            }
            if (widest > 0) {
                qreal &stored = syllableWidth[position.tickInMeasure];
                stored = std::max(stored, widest);
            }
        }
    }

    MeasureGrid grid;
    grid.ticks = QList<int>(tickSet.constBegin(), tickSet.constEnd());
    std::sort(grid.ticks.begin(), grid.ticks.end());
    while (!grid.ticks.isEmpty() && grid.ticks.last() >= measureTicks)
        grid.ticks.removeLast();
    if (grid.ticks.isEmpty())
        grid.ticks.append(0);

    qreal offset = 0;
    for (qsizetype i = 0; i < grid.ticks.size(); ++i) {
        grid.offsets.append(offset);
        const int next = i + 1 < grid.ticks.size() ? grid.ticks.at(i + 1) : measureTicks;
        const int duration = std::max(1, next - grid.ticks.at(i));
        // Longer notes get more room, but sublinearly — the engraver's rule, and
        // the reason a whole note is not sixteen times the width of a sixteenth.
        const qreal quarters = duration / static_cast<qreal>(ticks::Quarter);
        const qreal natural = space * (2.0 + 2.6 * std::pow(quarters, 0.55));
        const qreal words = syllableWidth.value(grid.ticks.at(i), 0.0) + syllableGap;
        offset += std::max(natural, words);
    }
    grid.total = std::max<qreal>(offset, space);
    return grid;
}

qreal MeasureGrid::offsetAt(int tick) const
{
    if (ticks.isEmpty())
        return 0;
    for (qsizetype i = ticks.size() - 1; i >= 0; --i) {
        if (ticks.at(i) <= tick)
            return offsets.at(i);
    }
    return offsets.first();
}

qreal ScoreView::xForTick(const MeasureBox &box, int tick) const
{
    const qreal usable = std::max<qreal>(1.0, box.width - 1.6 * m_staffSpace);
    return box.x + 1.1 * m_staffSpace + box.grid.offsetAt(tick) * usable / box.grid.total;
}

void ScoreView::setPlaybackTick(int tick)
{
    m_playbackTick = tick;
    viewport()->update();
}

void ScoreView::clearPlaybackTick()
{
    m_playbackTick = -1;
    viewport()->update();
}

void ScoreView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    relayout();
}

void ScoreView::relayout()
{
    m_systems.clear();
    m_boxes.clear();
    m_measureToSystem.clear();

    if (!m_session->isOpen()) {
        viewport()->update();
        return;
    }

    const SongDocument &doc = m_session->effectiveDocument();
    const qreal space = m_staffSpace;
    const qreal available
        = std::max<qreal>(240.0, viewport()->width() - (LeftMarginSpaces + RightMarginSpaces) * space);

    // Staff groups, ordered by staff_number, with the parts that share each.
    QList<StaffBox> staffTemplate;
    QList<int> staffNumbers;
    for (int i = 0; i < doc.parts.size(); ++i) {
        const int number = doc.parts.at(i).staffNumber.valueOr(i + 1);
        if (!staffNumbers.contains(number))
            staffNumbers.append(number);
    }
    std::sort(staffNumbers.begin(), staffNumbers.end());
    for (const int number : staffNumbers) {
        StaffBox staff;
        staff.staffNumber = number;
        for (int i = 0; i < doc.parts.size(); ++i) {
            if (doc.parts.at(i).staffNumber.valueOr(i + 1) == number) {
                staff.partIndices.append(i);
                if (staff.clef.isEmpty())
                    staff.clef = doc.parts.at(i).clef.valueOr(QStringLiteral("treble"));
            }
        }
        staffTemplate.append(staff);
    }

    const int measureCount = doc.measureCount();
    if (measureCount == 0) {
        viewport()->update();
        return;
    }

    // Measure widths follow their tick content, so a 4/4 bar of sixteenths gets
    // the room it needs and a whole-note bar does not waste any.
    QList<MeasureGrid> grids;
    QList<qreal> measureWidths;
    for (int m = 0; m < measureCount; ++m) {
        grids.append(buildGrid(m));
        measureWidths.append(std::min(available, grids.last().total + 1.6 * space));
    }

    // Break systems at phrase breaks when asked, otherwise fill the width.
    QList<int> breakAfterMeasure;
    if (m_phrased) {
        const auto collect = [&](const Field<QList<PhraseBreak>> &field) {
            if (!field.present())
                return;
            for (const PhraseBreak &brk : *field) {
                const int expected = doc.expectedTicksForMeasure(brk.measure);
                if (ticks::fromPhraseTicks(brk.tick) >= expected)
                    breakAfterMeasure.append(brk.measure - 1);
            }
        };
        collect(doc.phraseBreaks);
        collect(doc.optionalPhraseBreaks);
        std::sort(breakAfterMeasure.begin(), breakAfterMeasure.end());
    }

    qreal y = TopMarginSpaces * space;
    int first = 0;
    while (first < measureCount) {
        SystemBox system;
        system.firstMeasure = first;
        system.top = y;

        qreal used = 0;
        int last = first;
        for (int m = first; m < measureCount; ++m) {
            const qreal width = measureWidths.at(m);
            if (m > first && used + width > available)
                break;
            used += width;
            last = m;
            if (m_phrased && breakAfterMeasure.contains(m))
                break;
        }
        system.lastMeasure = last;

        // Stretch the system to the full width unless it is the last one.
        const bool stretch = last + 1 < measureCount;
        const qreal scale = (stretch && used > 0) ? available / used : 1.0;
        qreal x = LeftMarginSpaces * space;
        for (int m = first; m <= last; ++m) {
            MeasureBox box;
            box.index = m;
            box.x = x;
            box.width = measureWidths.at(m) * scale;
            box.grid = grids.at(m);
            box.expectedTicks = doc.expectedTicksForMeasure(m + 1);
            box.actualTicks = box.expectedTicks;
            for (const Part &part : doc.parts) {
                if (m < part.stream.measureCount()) {
                    const int actual = part.stream.measures().at(m).playedTicks();
                    if (actual != box.expectedTicks) {
                        box.ticksMatch = false;
                        box.actualTicks = actual;
                    }
                }
            }
            x += box.width;
            system.measures.append(box);
            m_measureToSystem.insert(m, static_cast<int>(m_systems.size()));
        }

        // Staves, then a lyric row per verse of the parts on the last staff.
        qreal staffY = y;
        for (StaffBox staff : staffTemplate) {
            staff.top = staffY;
            system.staves.append(staff);
            staffY += StaffGapSpaces * space;
        }

        int lyricRows = 0;
        for (const Part &part : doc.parts) {
            const PartAlignment &alignment = m_session->alignment(part.name);
            int rows = 0;
            for (const AttachedSection &section : alignment.sections) {
                if (showsSection(section))
                    ++rows;
            }
            lyricRows = std::max(lyricRows, rows);
        }
        system.lyricRows = lyricRows;
        // The last staff occupies four spaces below its top; lyrics start under
        // that, and the ruler sits clear of the deepest verse row.
        system.lyricTop = staffY - StaffGapSpaces * space + 5.4 * space;
        staffY = system.lyricTop + lyricRows * LyricRowSpaces * space;
        system.rulerTop = staffY + 1.0 * space;
        staffY = system.rulerTop + RulerSpaces * space;

        system.height = staffY - y;
        y = staffY + SystemGapSpaces * space;
        m_systems.append(system);
        first = last + 1;
    }

    m_contentHeight = y;
    m_contentWidth = viewport()->width();
    verticalScrollBar()->setRange(0, std::max<int>(0,
        static_cast<int>(m_contentHeight) - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(static_cast<int>(space * 4));
    viewport()->update();
}

qreal ScoreView::staffPositionFor(const Pitch &pitch, const QString &clef) const
{
    // Half a staff space per diatonic step, measured up from the bottom line.
    return (pitch.diatonic() - bottomLineDiatonic(clef)) * 0.5;
}

void ScoreView::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(viewport()->rect(), Qt::white);

    if (!m_session->isOpen()) {
        painter.setPen(QColor(0x88, 0x88, 0x88));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
            tr("No song open.\nPick one from the Songs list on the left."));
        return;
    }

    painter.translate(0, -verticalScrollBar()->value());
    m_boxes.clear();

    for (const SystemBox &system : m_systems) {
        const qreal viewTop = verticalScrollBar()->value();
        if (system.top + system.height < viewTop
            || system.top > viewTop + viewport()->height())
            continue;
        paintSystem(painter, system);
    }
}

void ScoreView::paintSystem(QPainter &painter, const SystemBox &system)
{
    paintMeasureProblems(painter, system);
    for (const StaffBox &staff : system.staves)
        paintStaff(painter, system, staff);
    paintPhraseRuler(painter, system);
}

void ScoreView::paintStaff(QPainter &painter, const SystemBox &system, const StaffBox &staff)
{
    const SongDocument &doc = m_session->effectiveDocument();
    const qreal space = m_staffSpace;
    const qreal left = system.measures.isEmpty() ? 0 : system.measures.first().x;
    const qreal right = system.measures.isEmpty()
        ? 0
        : system.measures.last().x + system.measures.last().width;

    painter.setPen(QPen(colorStaff(), 1.0));
    for (int line = 0; line < 5; ++line) {
        const qreal y = staff.top + line * space;
        painter.drawLine(QPointF(left, y), QPointF(right, y));
    }

    // Barlines.
    for (const MeasureBox &measure : system.measures) {
        painter.drawLine(QPointF(measure.x, staff.top),
            QPointF(measure.x, staff.top + 4 * space));
    }
    painter.drawLine(QPointF(right, staff.top), QPointF(right, staff.top + 4 * space));

    // Clef, drawn so its reference line lands where it belongs.
    painter.save();
    painter.setBrush(colorText());
    painter.setPen(Qt::NoPen);
    if (staff.clef == QLatin1String("bass")) {
        // F line is the second from the top.
        painter.translate(left - 5.2 * space, staff.top + 1 * space);
        painter.scale(space, space);
        painter.drawPath(glyphs::bassClef());
    } else {
        // G line is the second from the bottom.
        painter.translate(left - 5.2 * space, staff.top + 3 * space);
        painter.scale(space, space);
        painter.drawPath(glyphs::trebleClef());
    }
    painter.restore();
    if (staff.clef == QLatin1String("treble_8")) {
        QFont font = painter.font();
        font.setPointSizeF(space * 1.2);
        painter.setFont(font);
        painter.setPen(colorText());
        painter.drawText(QPointF(left - 5.0 * space, staff.top + 5.6 * space),
            QStringLiteral("8"));
    }

    // Part names, so a staff carrying two voices is readable.
    QFont label = painter.font();
    label.setPointSizeF(space * 1.05);
    painter.setFont(label);
    QStringList names;
    for (const int partIndex : staff.partIndices)
        names.append(doc.parts.at(partIndex).name);
    painter.setPen(QColor(0x77, 0x77, 0x77));
    painter.drawText(QPointF(2, staff.top - 0.4 * space), names.join(u'/'));

    // Voices sharing a staff take opposing stems, as the LilyPond export does.
    const bool shared = staff.partIndices.size() > 1;
    for (int i = 0; i < staff.partIndices.size(); ++i) {
        const int partIndex = staff.partIndices.at(i);
        paintPart(painter, system, staff, partIndex, shared && i == 0, shared && i > 0);
    }

    // Lyrics under the bottom staff of the system.
    if (&staff == &system.staves.last()) {
        for (const int partIndex : staff.partIndices)
            paintLyrics(painter, system, staff, partIndex);
    }
}

void ScoreView::paintPart(QPainter &painter, const SystemBox &system, const StaffBox &staff,
    int partIndex, bool forceStemUp, bool forceStemDown)
{
    const SongDocument &doc = m_session->effectiveDocument();
    const Part &part = doc.parts.at(partIndex);
    const qreal space = m_staffSpace;
    const Selection selection = m_session->selection();
    const bool ghosted = part.inheritedFromBase && part.notesInherited && doc.isOverlay;

    struct Placed {
        const Event *event = nullptr;
        QPointF head;
        bool stemUp = true;
        int measureIndex = 0;
        int eventIndex = 0;
    };
    QList<Placed> placed;

    for (const MeasureBox &measureBox : system.measures) {
        if (measureBox.index >= part.stream.measureCount())
            continue;
        const Measure &measure = part.stream.measures().at(measureBox.index);
        if (measure.events.isEmpty())
            continue;

        // Events sit on the measure's shared spacing grid, so voices line up
        // vertically and every syllable has room.
        int tick = 0;
        for (int e = 0; e < measure.events.size(); ++e) {
            const Event &event = measure.events.at(e);
            const qreal x = xForTick(measureBox, tick);
            tick += event.playedTicks();

            const bool selected = selection.partIndex == partIndex
                && selection.measureIndex == measureBox.index && selection.eventIndex == e;

            QColor ink = ghosted ? colorInherited() : colorText();
            if (selected)
                ink = colorSelection();
            if (m_playbackTick >= 0) {
                // Highlight what is sounding right now.
                const int absolute = event.tickInMeasure;
                Q_UNUSED(absolute);
            }

            if (event.isRest() || event.isSpacer()) {
                if (selected) {
                    painter.save();
                    painter.setPen(QPen(colorSelection(), 1.5));
                    painter.setBrush(colorSelectionFill());
                    painter.drawEllipse(QPointF(x, staff.top + 2 * space),
                        1.35 * space, 1.35 * space);
                    painter.restore();
                }
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(event.isSpacer() ? colorSpacer() : ink);
                painter.translate(x, staff.top + 2 * space);
                painter.scale(space, space);
                if (event.isSpacer()) {
                    // Spacers are invisible in print; here they are drawn faintly
                    // so pickup padding is not a mystery.
                    QPainterPath outline = glyphs::rest(event.duration.base);
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(colorSpacer(), 0.09, Qt::DotLine));
                    painter.drawPath(outline);
                } else {
                    painter.drawPath(glyphs::rest(event.duration.base));
                }
                painter.restore();
                if (event.duration.dots > 0) {
                    painter.setBrush(ink);
                    painter.setPen(Qt::NoPen);
                    for (int d = 0; d < event.duration.dots; ++d)
                        painter.drawEllipse(
                            QPointF(x + (0.9 + d * 0.4) * space, staff.top + 1.5 * space),
                            0.13 * space, 0.13 * space);
                }
                EventBox box;
                box.partIndex = partIndex;
                box.measureIndex = measureBox.index;
                box.eventIndex = e;
                box.rect = QRectF(x - space, staff.top, 2 * space, 4 * space);
                box.head = QPointF(x, staff.top + 2 * space);
                m_boxes.append(box);
                continue;
            }

            // Pitched: draw every notehead of the chord, stem from the primary.
            // Invalid empty chords are reported by the validator but must
            // remain safe to display while the author fixes the source.
            const qreal primaryPosition = event.pitches.isEmpty()
                ? 0.0
                : staffPositionFor(event.pitches.first(), staff.clef);
            const bool stemUp = forceStemUp ? true
                : forceStemDown              ? false
                                             : primaryPosition < 4.0;
            const QString keySignature = doc.keySignature.valueOr(QStringLiteral("C"));
            for (int p = 0; p < event.pitches.size(); ++p) {
                const Pitch &pitch = event.pitches.at(p);
                const qreal position = staffPositionFor(pitch, staff.clef);
                const qreal y = staff.top + 4 * space - position * space;

                if (selected) {
                    painter.save();
                    painter.setPen(QPen(colorSelection(), 1.5));
                    painter.setBrush(colorSelectionFill());
                    painter.drawEllipse(QPointF(x, y), 1.35 * space, 1.35 * space);
                    painter.restore();
                }

                // Ledger lines.
                painter.setPen(QPen(colorStaff(), 1.0));
                for (qreal ledger = 5; ledger <= position; ledger += 1.0)
                    painter.drawLine(QPointF(x - 0.95 * space, staff.top + 4 * space - ledger * space),
                        QPointF(x + 0.95 * space, staff.top + 4 * space - ledger * space));
                for (qreal ledger = -1; ledger >= position; ledger -= 1.0)
                    painter.drawLine(QPointF(x - 0.95 * space, staff.top + 4 * space - ledger * space),
                        QPointF(x + 0.95 * space, staff.top + 4 * space - ledger * space));

                if (pitch.alter != 0) {
                    painter.save();
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(ink);
                    painter.translate(x - 1.5 * space, y);
                    painter.scale(space, space);
                    painter.drawPath(pitch.alter == 1 ? glyphs::sharp()
                            : pitch.alter == -1      ? glyphs::flat()
                            : pitch.alter == 2       ? glyphs::doubleSharp()
                                                     : glyphs::doubleFlat());
                    painter.restore();
                }

                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink);
                painter.translate(x, y);
                painter.scale(space, space);
                const glyphs::AikenShape shape
                    = glyphs::aikenShapeForPitch(pitch.step, keySignature);
                painter.drawPath(
                    glyphs::aikenNotehead(shape, event.duration.base >= 4, stemUp));
                painter.restore();

                for (int d = 0; d < event.duration.dots; ++d) {
                    painter.setBrush(ink);
                    painter.setPen(Qt::NoPen);
                    painter.drawEllipse(QPointF(x + (0.95 + d * 0.4) * space, y - 0.1 * space),
                        0.13 * space, 0.13 * space);
                }
            }

            const qreal headY = staff.top + 4 * space - primaryPosition * space;
            if (event.duration.base >= 2) {
                const qreal stemX = x + (stemUp ? 0.62 : -0.62) * space;
                const qreal stemEnd = headY + (stemUp ? -3.3 : 3.3) * space;
                painter.setPen(QPen(ink, std::max(1.0, space * 0.13)));
                painter.drawLine(QPointF(stemX, headY), QPointF(stemX, stemEnd));

                // Flags, but only where no beam covers this note: the exporter
                // runs \autoBeamOff, so a beam exists only where [ ] says so.
                const int flags = flagCountFor(event.duration.base);
                const bool beamed = event.beamStart || event.beamEnd;
                if (flags > 0 && !beamed) {
                    painter.save();
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(ink);
                    painter.translate(stemX, stemEnd);
                    painter.scale(space, space);
                    painter.drawPath(glyphs::flag(flags, stemUp));
                    painter.restore();
                }
            }

            if (event.fermata) {
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink);
                painter.translate(x, staff.top - 1.2 * space);
                painter.scale(space, space);
                painter.drawPath(glyphs::fermata(true));
                painter.restore();
            }
            if (event.staccato) {
                painter.setBrush(ink);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(QPointF(x, headY + (stemUp ? 1.1 : -1.1) * space),
                    0.14 * space, 0.14 * space);
            }
            if (!event.dynamic.isEmpty()) {
                QFont font = painter.font();
                font.setItalic(true);
                font.setBold(true);
                font.setPointSizeF(space * 1.4);
                painter.setFont(font);
                painter.setPen(ink);
                painter.drawText(QPointF(x - 0.5 * space, staff.top + 6.6 * space),
                    event.dynamic);
            }
            if (!event.tempoSpanner.isEmpty()) {
                QFont font = painter.font();
                font.setItalic(true);
                font.setPointSizeF(space * 1.3);
                painter.setFont(font);
                painter.setPen(ink);
                painter.drawText(QPointF(x - 0.5 * space, staff.top - 1.6 * space),
                    event.tempoSpanner + u'.');
            }
            if (event.chorusStart || event.codaStart) {
                QFont font = painter.font();
                font.setBold(true);
                font.setPointSizeF(space * 1.1);
                painter.setFont(font);
                painter.setPen(colorPlayback());
                painter.drawText(QPointF(x - 0.4 * space, staff.top - 2.6 * space),
                    event.chorusStart ? QStringLiteral("@c") : QStringLiteral("@e"));
            }

            placed.append(Placed { &event, QPointF(x, headY), stemUp, measureBox.index, e });

            EventBox box;
            box.partIndex = partIndex;
            box.measureIndex = measureBox.index;
            box.eventIndex = e;
            box.rect = QRectF(x - space, headY - space, 2 * space, 2 * space);
            box.head = QPointF(x, headY);
            box.stemUp = stemUp;
            m_boxes.append(box);

        }
    }

    // Beams, ties, and slurs drawn over the placed notes of this system.
    painter.setPen(Qt::NoPen);
    const QColor ink = ghosted ? colorInherited() : colorText();
    for (int i = 0; i < placed.size(); ++i) {
        const Placed &from = placed.at(i);
        if (from.event->beamStart) {
            for (int j = i + 1; j < placed.size(); ++j) {
                const Placed &to = placed.at(j);
                const qreal y1 = from.head.y() + (from.stemUp ? -3.3 : 3.3) * space;
                const qreal y2 = to.head.y() + (to.stemUp ? -3.3 : 3.3) * space;
                const qreal x1 = from.head.x() + (from.stemUp ? 0.62 : -0.62) * space;
                const qreal x2 = to.head.x() + (to.stemUp ? 0.62 : -0.62) * space;
                painter.setBrush(ink);
                QPainterPath beam;
                const qreal thickness = 0.45 * space;
                beam.moveTo(x1, y1);
                beam.lineTo(x2, y2);
                beam.lineTo(x2, y2 + (from.stemUp ? thickness : -thickness));
                beam.lineTo(x1, y1 + (from.stemUp ? thickness : -thickness));
                beam.closeSubpath();
                painter.drawPath(beam);
                if (to.event->beamEnd)
                    break;
            }
        }
        const bool slurLike = from.event->slurStart || from.event->dashedSlurStart;
        if (slurLike) {
            for (int j = i + 1; j < placed.size(); ++j) {
                const Placed &to = placed.at(j);
                const bool ends = from.event->slurStart ? to.event->slurEnd : to.event->dashedSlurEnd;
                if (!ends && j + 1 < placed.size())
                    continue;
                QPainterPath curve;
                const qreal lift = 2.0 * space;
                curve.moveTo(from.head.x(), from.head.y() - lift * 0.5);
                curve.quadTo((from.head.x() + to.head.x()) / 2,
                    std::min(from.head.y(), to.head.y()) - lift,
                    to.head.x(), to.head.y() - lift * 0.5);
                QPen pen(ink, std::max(1.0, space * 0.11));
                if (from.event->dashedSlurStart)
                    pen.setStyle(Qt::DashLine);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(curve);
                break;
            }
        }
        if (from.event->tie && i + 1 < placed.size()) {
            const Placed &to = placed.at(i + 1);
            QPainterPath curve;
            curve.moveTo(from.head.x() + 0.6 * space, from.head.y() + 0.9 * space);
            curve.quadTo((from.head.x() + to.head.x()) / 2, from.head.y() + 1.8 * space,
                to.head.x() - 0.6 * space, to.head.y() + 0.9 * space);
            painter.setPen(QPen(ink, std::max(1.0, space * 0.11)));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(curve);
        }
    }
}

void ScoreView::paintLyrics(
    QPainter &painter, const SystemBox &system, const StaffBox &staff, int partIndex)
{
    const SongDocument &doc = m_session->effectiveDocument();
    const Part &part = doc.parts.at(partIndex);
    const PartAlignment &alignment = m_session->alignment(part.name);
    if (alignment.sections.isEmpty())
        return;

    const qreal space = m_staffSpace;
    const QFont font = lyricFont();
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    // Which slots fall inside this system? Slots carry their measure, so the
    // question is just which measures the system covers.
    int row = -1;
    for (const AttachedSection &section : alignment.sections) {
        if (!showsSection(section))
            continue;
        ++row;
        const bool active = section.isChorus || section.isCoda
            || section.verseNumber == m_verse;
        painter.setPen(active ? colorText() : QColor(0x8a, 0x8a, 0x8a));
        const qreal y = system.lyricTop + row * LyricRowSpaces * space;

        for (int slot = section.slotOffset;
            slot < section.slotOffset + section.syllables.size(); ++slot) {
            if (slot < 0 || slot >= alignment.lyricSlots.size())
                continue;
            const Slot &position = alignment.lyricSlots.at(slot);
            if (position.measureIndex < system.firstMeasure
                || position.measureIndex > system.lastMeasure)
                continue;

            // Find where that event was drawn.
            const auto box = std::find_if(m_boxes.begin(), m_boxes.end(), [&](const EventBox &b) {
                return b.partIndex == partIndex && b.measureIndex == position.measureIndex
                    && b.eventIndex == position.eventIndex;
            });
            if (box == m_boxes.end())
                continue;
            const QString syllable = alignment.syllableAt(section, slot);
            if (syllable.isEmpty())
                continue;
            painter.drawText(
                QPointF(box->head.x() - metrics.horizontalAdvance(syllable) / 2, y), syllable);
        }

        // Row label at the left margin.
        painter.setPen(QColor(0x99, 0x99, 0x99));
        painter.drawText(QPointF(2, y),
            section.isChorus ? QStringLiteral("ch.")
                : section.isCoda ? QStringLiteral("coda")
                                 : QString::number(section.verseNumber) + u'.');
    }
}

void ScoreView::paintPhraseRuler(QPainter &painter, const SystemBox &system)
{
    const SongDocument &doc = m_session->effectiveDocument();
    const qreal space = m_staffSpace;
    const qreal baseY = system.rulerTop;

    struct Lane {
        const Field<QList<PhraseBreak>> *field;
        QColor color;
        QString label;
    };
    const Lane lanes[3] = {
        { &doc.phraseBreaks, QColor(0x1f, 0x6f, 0xeb), tr("required") },
        { &doc.optionalPhraseBreaks, QColor(0x99, 0x77, 0x11), tr("optional") },
        { &doc.nonBreakingPhraseBreaks, QColor(0x88, 0x88, 0x88), tr("dedup only") },
    };

    QFont font = painter.font();
    font.setPointSizeF(space * 0.95);
    painter.setFont(font);

    for (int lane = 0; lane < 3; ++lane) {
        const qreal y = baseY + lane * 1.3 * space;
        const bool hovered = lane == m_hoverLane;
        painter.setPen(hovered ? QColor(0xbb, 0xbb, 0xbb) : QColor(0xdd, 0xdd, 0xdd));
        if (!system.measures.isEmpty()) {
            painter.drawLine(QPointF(system.measures.first().x, y),
                QPointF(system.measures.last().x + system.measures.last().width, y));
        }
        painter.setPen(hovered ? lanes[lane].color : QColor(0xaa, 0xaa, 0xaa));
        painter.drawText(QPointF(2, y + 0.35 * space), lanes[lane].label);

        // Show where a click would land, snapped, before it lands there.
        if (hovered && m_hoverMeasure >= 0) {
            const auto box = std::find_if(system.measures.begin(), system.measures.end(),
                [this](const MeasureBox &m) { return m.index == m_hoverMeasure; });
            if (box != system.measures.end()) {
                const int measureTicks
                    = std::max(1, doc.expectedTicksForMeasure(m_hoverMeasure + 1));
                const qreal x = m_hoverTick >= measureTicks ? box->x + box->width
                                                            : xForTick(*box, m_hoverTick);
                QColor ghost = lanes[lane].color;
                ghost.setAlpha(110);
                painter.setPen(QPen(ghost, 2.0, Qt::DotLine));
                painter.drawLine(QPointF(x, y - 0.55 * space), QPointF(x, y + 0.55 * space));
            }
        }

        if (!lanes[lane].field->present())
            continue;
        for (const PhraseBreak &brk : **lanes[lane].field) {
            const int measureIndex = brk.measure - 1;
            if (measureIndex < system.firstMeasure || measureIndex > system.lastMeasure)
                continue;
            const auto measureBox = std::find_if(system.measures.begin(), system.measures.end(),
                [measureIndex](const MeasureBox &m) { return m.index == measureIndex; });
            if (measureBox == system.measures.end())
                continue;
            // A break is written as the tick it falls *after*, so it lands on the
            // grid position of the note that follows it — or at the barline.
            const int internal = ticks::fromPhraseTicks(brk.tick);
            const int expected = std::max(1, doc.expectedTicksForMeasure(brk.measure));
            const qreal x = internal >= expected
                ? measureBox->x + measureBox->width
                : xForTick(*measureBox, internal);
            painter.setPen(QPen(lanes[lane].color, 2.0));
            painter.drawLine(QPointF(x, y - 0.45 * space), QPointF(x, y + 0.45 * space));
        }
    }
}

void ScoreView::paintMeasureProblems(QPainter &painter, const SystemBox &system)
{
    const qreal space = m_staffSpace;
    for (const MeasureBox &measure : system.measures) {
        if (measure.ticksMatch)
            continue;
        painter.fillRect(QRectF(measure.x, system.top - 1.0 * space, measure.width,
                             system.height - RulerSpaces * space),
            QColor(0xd1, 0x24, 0x2f, 18));
        QFont font = painter.font();
        font.setPointSizeF(space * 1.1);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(colorProblem());
        const int delta = measure.actualTicks - measure.expectedTicks;
        painter.drawText(QPointF(measure.x + 0.4 * space, system.top - 1.4 * space),
            QStringLiteral("%1%2 ticks")
                .arg(delta > 0 ? QStringLiteral("+") : QString())
                .arg(ticks::toPhraseTicks(delta)));
    }
}

const EventBox *ScoreView::hitTest(QPointF point) const
{
    const EventBox *best = nullptr;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (const EventBox &box : m_boxes) {
        if (!box.rect.adjusted(-2, -6, 2, 6).contains(point))
            continue;
        const qreal distance = QLineF(point, box.head).length();
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &box;
        }
    }
    return best;
}

int ScoreView::measureAtX(const SystemBox &system, qreal x) const
{
    for (const MeasureBox &measure : system.measures) {
        if (x >= measure.x && x < measure.x + measure.width)
            return measure.index;
    }
    return -1;
}

void ScoreView::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    const QPointF point = event->position() + QPointF(0, verticalScrollBar()->value());
    if (clickRuler(point))
        return;
    if (const EventBox *box = hitTest(point)) {
        m_session->setSelection(Selection { box->partIndex, box->measureIndex, box->eventIndex });
        viewport()->update();
    }
}

void ScoreView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPointF point = event->position() + QPointF(0, verticalScrollBar()->value());
    if (const EventBox *box = hitTest(point)) {
        const SongDocument &doc = m_session->effectiveDocument();
        if (box->partIndex < doc.parts.size()) {
            const Part &part = doc.parts.at(box->partIndex);
            const PartAlignment &alignment = m_session->alignment(part.name);
            for (int slot = 0; slot < alignment.lyricSlots.size(); ++slot) {
                const Slot &position = alignment.lyricSlots.at(slot);
                if (position.measureIndex == box->measureIndex
                    && position.eventIndex == box->eventIndex) {
                    Q_EMIT requestLyricEdit(part.name, slot);
                    return;
                }
            }
        }
    }
}

void ScoreView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const qreal factor = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        m_staffSpace = std::clamp<qreal>(m_staffSpace * factor, 4.0, 20.0);
        relayout();
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

// ---------------------------------------------------------------- editing ---

Event *ScoreView::mutableSelectedEvent(SongDocument &doc) const
{
    const Selection selection = m_session->selection();
    if (!selection.hasEvent())
        return nullptr;
    // The selection indexes the effective document; find the same part by name
    // so an overlay edits its own copy rather than the base's.
    const SongDocument &effective = m_session->effectiveDocument();
    if (selection.partIndex >= effective.parts.size())
        return nullptr;
    const QString partName = effective.parts.at(selection.partIndex).name;
    Part *part = doc.part(partName);
    if (!part || selection.measureIndex >= part->stream.measureCount())
        return nullptr;
    Measure &measure = part->stream.measures()[selection.measureIndex];
    if (selection.eventIndex >= measure.events.size())
        return nullptr;
    return &measure.events[selection.eventIndex];
}

void ScoreView::moveSelection(int deltaEvents)
{
    const SongDocument &doc = m_session->effectiveDocument();
    Selection selection = m_session->selection();
    if (!selection.isValid()) {
        if (!doc.parts.isEmpty() && doc.parts.first().stream.measureCount() > 0)
            m_session->setSelection(Selection { 0, 0, 0 });
        return;
    }
    const Part &part = doc.parts.at(selection.partIndex);
    int measure = selection.measureIndex;
    int index = selection.eventIndex + deltaEvents;
    while (index < 0) {
        if (--measure < 0)
            return;
        index += static_cast<int>(part.stream.measures().at(measure).events.size());
    }
    while (measure < part.stream.measureCount()
        && index >= part.stream.measures().at(measure).events.size()) {
        index -= static_cast<int>(part.stream.measures().at(measure).events.size());
        if (++measure >= part.stream.measureCount())
            return;
    }
    m_session->setSelection(Selection { selection.partIndex, measure, index });
}

void ScoreView::moveSelectionToPart(int deltaParts)
{
    const SongDocument &doc = m_session->effectiveDocument();
    Selection selection = m_session->selection();
    if (!selection.isValid())
        return;
    const int target = std::clamp<int>(selection.partIndex + deltaParts, 0,
        static_cast<int>(doc.parts.size()) - 1);
    const Part &part = doc.parts.at(target);
    if (selection.measureIndex >= part.stream.measureCount())
        return;
    const int events = static_cast<int>(part.stream.measures().at(selection.measureIndex).events.size());
    m_session->setSelection(Selection { target, selection.measureIndex,
        std::min(selection.eventIndex, events - 1) });
}

void ScoreView::transposeSelection(int diatonicSteps, int alterDelta, int octaves)
{
    m_session->mutate(tr("Change pitch"), [&](SongDocument &doc) {
        Event *event = mutableSelectedEvent(doc);
        if (!event || event->pitches.isEmpty())
            return;
        for (Pitch &pitch : event->pitches) {
            if (octaves != 0) {
                pitch.octave += octaves;
            } else if (diatonicSteps != 0) {
                pitch = Pitch::fromDiatonic(pitch.diatonic() + diatonicSteps, pitch.alter);
            } else if (alterDelta != 0) {
                pitch.alter = std::clamp(pitch.alter + alterDelta, -2, 2);
            }
        }
        event->dirty = true;
    });
}

void ScoreView::setSelectionDuration(int base)
{
    m_session->mutate(tr("Change duration"), [&](SongDocument &doc) {
        if (Event *event = mutableSelectedEvent(doc)) {
            event->duration.base = base;
            event->dirty = true;
        }
    });
}

void ScoreView::toggleSelectionDot()
{
    m_session->mutate(tr("Change dots"), [&](SongDocument &doc) {
        if (Event *event = mutableSelectedEvent(doc)) {
            event->duration.dots = (event->duration.dots + 1) % 3;
            event->dirty = true;
        }
    });
}

void ScoreView::toggleFlag(const QString &description, bool Event::*flag)
{
    m_session->mutate(description, [&](SongDocument &doc) {
        if (Event *event = mutableSelectedEvent(doc)) {
            event->*flag = !(event->*flag);
            event->dirty = true;
        }
    });
}

void ScoreView::convertSelection(EventKind kind)
{
    m_session->mutate(tr("Change note type"), [&](SongDocument &doc) {
        Event *event = mutableSelectedEvent(doc);
        if (!event)
            return;
        if (kind == EventKind::Rest || kind == EventKind::Spacer) {
            event->pitches.clear();
        } else if (event->pitches.isEmpty()) {
            Pitch pitch;
            pitch.step = u'C';
            pitch.octave = 4;
            event->pitches.append(pitch);
        }
        event->kind = kind;
        event->dirty = true;
    });
}

void ScoreView::insertAfterSelection()
{
    const Selection selection = m_session->selection();
    if (!selection.hasEvent())
        return;
    m_session->mutate(tr("Insert note"), [&](SongDocument &doc) {
        const SongDocument &effective = m_session->effectiveDocument();
        const QString partName = effective.parts.at(selection.partIndex).name;
        Part *part = doc.part(partName);
        if (!part || selection.measureIndex >= part->stream.measureCount())
            return;
        Measure &measure = part->stream.measures()[selection.measureIndex];
        if (selection.eventIndex >= measure.events.size())
            return;
        Event copy = measure.events.at(selection.eventIndex);
        copy.dirty = true;
        copy.raw.clear();
        copy.chorusStart = false;
        copy.codaStart = false;
        copy.slurStart = copy.slurEnd = copy.beamStart = copy.beamEnd = false;
        copy.dashedSlurStart = copy.dashedSlurEnd = false;
        copy.tie = false;
        measure.events.insert(selection.eventIndex + 1, copy);
        part->notes.set(part->stream.toSource());
    });
}

void ScoreView::deleteSelection()
{
    const Selection selection = m_session->selection();
    if (!selection.hasEvent())
        return;
    m_session->mutate(tr("Delete note"), [&](SongDocument &doc) {
        const SongDocument &effective = m_session->effectiveDocument();
        const QString partName = effective.parts.at(selection.partIndex).name;
        Part *part = doc.part(partName);
        if (!part || selection.measureIndex >= part->stream.measureCount())
            return;
        Measure &measure = part->stream.measures()[selection.measureIndex];
        if (selection.eventIndex >= measure.events.size() || measure.events.size() <= 1)
            return;
        measure.events.removeAt(selection.eventIndex);
        part->notes.set(part->stream.toSource());
    });
}

void ScoreView::togglePhraseBreakAtCursor(BreakKind kind)
{
    const Selection selection = m_session->selection();
    if (!selection.hasEvent())
        return;
    const SongDocument &effective = m_session->effectiveDocument();
    const Part &part = effective.parts.at(selection.partIndex);
    const Measure &measure = part.stream.measures().at(selection.measureIndex);

    // The break fires *after* the selected note, so the tick is the running
    // total through that note — computed here rather than left to the author.
    int tick = 0;
    for (int e = 0; e <= selection.eventIndex && e < measure.events.size(); ++e)
        tick += measure.events.at(e).playedTicks();

    const PhraseBreak brk { selection.measureIndex + 1, ticks::toPhraseTicks(tick) };
    m_session->togglePhraseBreak(brk, kind);
    Q_EMIT statusMessage(m_session->phraseBreakAt(brk)
            ? tr("Phrase break %1 (after the %2 note under the cursor)")
                  .arg(brk.toString(), part.name)
            : tr("Phrase break %1 removed").arg(brk.toString()));
}

int ScoreView::rulerLaneAt(const SystemBox &system, qreal y) const
{
    const qreal space = m_staffSpace;
    for (int lane = 0; lane < 3; ++lane) {
        const qreal centre = system.rulerTop + lane * 1.3 * space;
        if (y >= centre - 0.65 * space && y <= centre + 0.65 * space)
            return lane;
    }
    return -1;
}

bool ScoreView::rulerTargetAt(QPointF point, int &lane, int &measureIndex, int &tick) const
{
    for (const SystemBox &system : m_systems) {
        lane = rulerLaneAt(system, point.y());
        if (lane < 0)
            continue;
        measureIndex = -1;
        for (const MeasureBox &measure : system.measures) {
            if (point.x() < measure.x || point.x() >= measure.x + measure.width)
                continue;
            // Snap to the nearest note boundary: a break between notes is the
            // only kind the format can express, and the only kind that passes
            // R9.5. The measure's grid already holds every voice's note starts.
            const int measureTicks = std::max(1,
                m_session->effectiveDocument().expectedTicksForMeasure(measure.index + 1));
            QList<int> candidates = measure.grid.ticks;
            candidates.append(measureTicks);
            qreal bestDistance = std::numeric_limits<qreal>::max();
            for (const int candidate : candidates) {
                const qreal x = candidate >= measureTicks ? measure.x + measure.width
                                                          : xForTick(measure, candidate);
                const qreal distance = std::abs(x - point.x());
                if (distance < bestDistance) {
                    bestDistance = distance;
                    tick = candidate;
                }
            }
            measureIndex = measure.index;
            return true;
        }
        return true;  // in a lane but past the last measure
    }
    lane = -1;
    return false;
}

bool ScoreView::clickRuler(QPointF point)
{
    int lane = -1;
    int measureIndex = -1;
    int tick = 0;
    if (!rulerTargetAt(point, lane, measureIndex, tick))
        return false;
    if (measureIndex < 0)
        return true;

    const PhraseBreak brk { measureIndex + 1, ticks::toPhraseTicks(tick) };
    const BreakKind kind = lane == 0 ? BreakKind::Required
        : lane == 1                  ? BreakKind::Optional
                                     : BreakKind::NonBreaking;
    m_session->togglePhraseBreak(brk, kind);
    Q_EMIT statusMessage(m_session->phraseBreakAt(brk)
            ? tr("Phrase break %1").arg(brk.toString())
            : tr("Phrase break %1 removed").arg(brk.toString()));
    return true;
}

void ScoreView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF point = event->position() + QPointF(0, verticalScrollBar()->value());
    int lane = -1;
    int measureIndex = -1;
    int tick = 0;
    rulerTargetAt(point, lane, measureIndex, tick);
    if (lane != m_hoverLane || measureIndex != m_hoverMeasure || tick != m_hoverTick) {
        m_hoverLane = lane;
        m_hoverMeasure = measureIndex;
        m_hoverTick = tick;
        viewport()->setCursor(lane >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void ScoreView::applyMarkingToAllVoices()
{
    const Selection selection = m_session->selection();
    if (!selection.hasEvent())
        return;
    const SongDocument &effective = m_session->effectiveDocument();
    const Part &sourcePart = effective.parts.at(selection.partIndex);
    const Measure &measure = sourcePart.stream.measures().at(selection.measureIndex);
    const Event &source = measure.events.at(selection.eventIndex);

    int targetTick = 0;
    for (int e = 0; e < selection.eventIndex; ++e)
        targetTick += measure.events.at(e).playedTicks();

    // Dynamics, fermatas, and staccatos are required on every sounding voice;
    // this is that rule as a single command.
    m_session->mutate(tr("Apply marking to all voices"), [&](SongDocument &doc) {
        for (Part &part : doc.parts) {
            if (selection.measureIndex >= part.stream.measureCount())
                continue;
            Measure &target = part.stream.measures()[selection.measureIndex];
            int tick = 0;
            for (Event &event : target.events) {
                if (tick == targetTick && event.isSounding()) {
                    event.dynamic = source.dynamic;
                    event.hairpin = source.hairpin;
                    event.fermata = source.fermata;
                    event.staccato = source.staccato;
                    event.dirty = true;
                }
                tick += event.playedTicks();
            }
            part.notes.set(part.stream.toSource());
        }
    });
}

void ScoreView::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers();

    switch (key) {
    case Qt::Key_Left: moveSelection(-1); return;
    case Qt::Key_Right: moveSelection(1); return;
    case Qt::Key_Up:
        if (modifiers & Qt::AltModifier)
            moveSelectionToPart(-1);
        else if (modifiers & Qt::ControlModifier)
            transposeSelection(0, 0, 1);
        else if (modifiers & Qt::ShiftModifier)
            transposeSelection(0, 1, 0);
        else
            transposeSelection(1, 0, 0);
        return;
    case Qt::Key_Down:
        if (modifiers & Qt::AltModifier)
            moveSelectionToPart(1);
        else if (modifiers & Qt::ControlModifier)
            transposeSelection(0, 0, -1);
        else if (modifiers & Qt::ShiftModifier)
            transposeSelection(0, -1, 0);
        else
            transposeSelection(-1, 0, 0);
        return;
    case Qt::Key_1: setSelectionDuration(1); return;
    case Qt::Key_2: setSelectionDuration(2); return;
    case Qt::Key_4: setSelectionDuration(4); return;
    case Qt::Key_8: setSelectionDuration(8); return;
    case Qt::Key_6: setSelectionDuration(16); return;
    case Qt::Key_3: setSelectionDuration(32); return;
    case Qt::Key_Period: toggleSelectionDot(); return;
    case Qt::Key_R: convertSelection(EventKind::Rest); return;
    case Qt::Key_P: convertSelection(EventKind::Spacer); return;
    case Qt::Key_N: convertSelection(EventKind::Note); return;
    case Qt::Key_T: toggleFlag(tr("Toggle tie"), &Event::tie); return;
    case Qt::Key_F: toggleFlag(tr("Toggle fermata"), &Event::fermata); return;
    case Qt::Key_K: toggleFlag(tr("Toggle staccato"), &Event::staccato); return;
    case Qt::Key_C: toggleFlag(tr("Toggle chorus marker"), &Event::chorusStart); return;
    case Qt::Key_E: toggleFlag(tr("Toggle coda marker"), &Event::codaStart); return;
    case Qt::Key_S:
        toggleFlag(modifiers & Qt::ShiftModifier ? tr("Toggle slur end") : tr("Toggle slur start"),
            modifiers & Qt::ShiftModifier ? &Event::slurEnd : &Event::slurStart);
        return;
    case Qt::Key_B:
        if (modifiers & Qt::ControlModifier) {
            togglePhraseBreakAtCursor(modifiers & Qt::ShiftModifier ? BreakKind::Optional
                                                                    : BreakKind::Required);
            return;
        }
        if (modifiers & Qt::AltModifier) {
            togglePhraseBreakAtCursor(BreakKind::NonBreaking);
            return;
        }
        toggleFlag(modifiers & Qt::ShiftModifier ? tr("Toggle beam end") : tr("Toggle beam start"),
            modifiers & Qt::ShiftModifier ? &Event::beamEnd : &Event::beamStart);
        return;
    case Qt::Key_D:
        toggleFlag(modifiers & Qt::ShiftModifier ? tr("Toggle dashed slur end")
                                                 : tr("Toggle dashed slur start"),
            modifiers & Qt::ShiftModifier ? &Event::dashedSlurEnd : &Event::dashedSlurStart);
        return;
    case Qt::Key_Insert: insertAfterSelection(); return;
    case Qt::Key_Delete: deleteSelection(); return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (modifiers & Qt::ControlModifier) {
            applyMarkingToAllVoices();
            return;
        }
        break;
    default: break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

} // namespace ope::ui
