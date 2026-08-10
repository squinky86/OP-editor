// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The score: staves, lyrics, the phrase-break ruler, and the editing surface.
//
// This is an editing view, not an engraving. It shows exactly what the data
// says — including a measure that does not add up, a spacer used as padding, and
// which notes carry a syllable — and it never rearranges anything behind the
// author's back.

#pragma once

#include "app/Session.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QList>

namespace ope::ui {

/// Where one event ended up on the page.
struct EventBox {
    int partIndex = 0;
    int measureIndex = 0;
    int eventIndex = 0;
    QRectF rect;      ///< hit-test area
    QPointF head;     ///< notehead centre
    bool stemUp = true;
};

/// How wide each rhythmic position in a measure has to be.
///
/// Notes are *not* spaced in proportion to their ticks. Every part's note starts
/// are merged into one grid of tick positions, and each position is given the
/// larger of the room its duration wants and the room its syllable needs. That
/// is what stops "Each day I'll do" from printing as "Eachday I'lldo" under a
/// run of eighth notes, and it keeps the voices vertically aligned while it does
/// it, because they all read the same grid.
struct MeasureGrid {
    QList<int> ticks;      ///< ascending note-start ticks, always starting at 0
    QList<qreal> offsets;  ///< natural x of each tick, from the measure's content start
    qreal total = 1.0;     ///< natural width of the whole measure

    [[nodiscard]] qreal offsetAt(int tick) const;
};

/// One measure in one system.
struct MeasureBox {
    int index = 0;
    qreal x = 0;
    qreal width = 0;
    bool ticksMatch = true;
    int actualTicks = 0;
    int expectedTicks = 0;
    MeasureGrid grid;
};

/// A staff group: every part sharing one staff_number.
struct StaffBox {
    int staffNumber = 1;
    QList<int> partIndices;
    QString clef;
    qreal top = 0;  ///< y of the top staff line
};

/// One horizontal band of music.
struct SystemBox {
    int firstMeasure = 0;
    int lastMeasure = 0;
    qreal top = 0;
    qreal height = 0;
    qreal lyricTop = 0;   ///< first lyric row baseline
    qreal rulerTop = 0;   ///< first phrase-ruler lane
    int lyricRows = 0;
    QList<MeasureBox> measures;
    QList<StaffBox> staves;
};

class ScoreView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit ScoreView(Session *session, QWidget *parent = nullptr);

    void setPhrasedLayout(bool phrased);
    [[nodiscard]] bool phrasedLayout() const noexcept { return m_phrased; }
    void setActiveVerse(int verse);
    [[nodiscard]] int activeVerse() const noexcept { return m_verse; }
    /// Stack every verse under the staff, or only the one being worked on.
    void setShowAllVerses(bool all);
    [[nodiscard]] bool showAllVerses() const noexcept { return m_allVerses; }
    void setPlaybackTick(int tick);
    void clearPlaybackTick();
    void relayout();

Q_SIGNALS:
    void statusMessage(const QString &message);
    void requestLyricEdit(const QString &partName, int slot);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void paintSystem(QPainter &painter, const SystemBox &system);
    void paintStaff(QPainter &painter, const SystemBox &system, const StaffBox &staff);
    void paintPart(QPainter &painter, const SystemBox &system, const StaffBox &staff,
        int partIndex, bool forceStemUp, bool forceStemDown);
    void paintLyrics(QPainter &painter, const SystemBox &system, const StaffBox &staff,
        int partIndex);
    void paintPhraseRuler(QPainter &painter, const SystemBox &system);
    void paintMeasureProblems(QPainter &painter, const SystemBox &system);

    [[nodiscard]] qreal staffPositionFor(const Pitch &pitch, const QString &clef) const;
    /// True when this verse row is drawn under the staff at the moment.
    [[nodiscard]] bool showsSection(const AttachedSection &section) const;
    /// The font syllables are set in; layout needs its metrics as much as
    /// painting does, because syllable widths decide how wide a measure is.
    [[nodiscard]] QFont lyricFont() const;
    /// Build one measure's spacing grid from every part's rhythm and syllables.
    [[nodiscard]] MeasureGrid buildGrid(int measureIndex) const;
    /// Where a note starting at `tick` inside `box` is drawn.
    [[nodiscard]] qreal xForTick(const MeasureBox &box, int tick) const;
    [[nodiscard]] const EventBox *hitTest(QPointF point) const;
    [[nodiscard]] int measureAtX(const SystemBox &system, qreal x) const;

    // -- editing operations, each pushed as one undo step
    void moveSelection(int deltaEvents);
    void moveSelectionToPart(int deltaParts);
    void transposeSelection(int diatonicSteps, int alterDelta, int octaves);
    void setSelectionDuration(int base);
    void toggleSelectionDot();
    void toggleFlag(const QString &description, bool Event::*flag);
    void convertSelection(EventKind kind);
    void insertAfterSelection();
    void deleteSelection();
    void togglePhraseBreakAtCursor(BreakKind kind);
    void applyMarkingToAllVoices();

    [[nodiscard]] Event *mutableSelectedEvent(SongDocument &doc) const;

    Session *m_session = nullptr;
    QList<SystemBox> m_systems;
    QList<EventBox> m_boxes;
    QHash<int, int> m_measureToSystem;
    qreal m_staffSpace = 7.5;   ///< pixels per staff space; the zoom control
    qreal m_contentHeight = 0;
    qreal m_contentWidth = 0;
    bool m_phrased = true;
    bool m_allVerses = false;
    int m_verse = 1;
    int m_playbackTick = -1;
};

} // namespace ope::ui
