// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Glyphs.h"

#include <QPolygonF>
#include <QTransform>

#include <array>

namespace ope::ui::glyphs {
namespace {

/// An oval notehead: an ellipse tilted the way a broad-nib pen would draw it.
QPainterPath tiltedOval(qreal width, qreal height, qreal degrees)
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), width / 2.0, height / 2.0);
    return QTransform().rotate(degrees).map(path);
}

QPainterPath polygon(std::initializer_list<QPointF> points)
{
    QPainterPath path;
    const QPolygonF polygon(points);
    path.addPolygon(polygon);
    path.closeSubpath();
    return path;
}

QPainterPath hollowed(const QPainterPath &outer, const QPainterPath &inner, bool filled)
{
    return filled ? outer : outer.subtracted(inner);
}

} // namespace

AikenShape aikenShapeForPitch(QChar pitchStep, QStringView keySignature)
{
    const auto stepIndex = [](QChar step) {
        switch (step.toUpper().toLatin1()) {
        case 'C': return 0;
        case 'D': return 1;
        case 'E': return 2;
        case 'F': return 3;
        case 'G': return 4;
        case 'A': return 5;
        case 'B': return 6;
        default: return 0;
        }
    };

    // The key's first letter is its tonic. For la-based minor, moving forward
    // two letter names finds the relative-major tonic: A -> C, E -> G, etc.
    int tonic = keySignature.isEmpty() ? 0 : stepIndex(keySignature.front());
    if (keySignature.endsWith(u'm', Qt::CaseInsensitive))
        tonic = (tonic + 2) % 7;

    const int degree = (stepIndex(pitchStep) - tonic + 7) % 7;
    static constexpr std::array shapes { AikenShape::Do, AikenShape::Re, AikenShape::Mi,
        AikenShape::Fa, AikenShape::Sol, AikenShape::La, AikenShape::Ti };
    return shapes.at(static_cast<std::size_t>(degree));
}

QPainterPath aikenNotehead(AikenShape shape, bool filled, bool stemUp)
{
    switch (shape) {
    case AikenShape::Do: {
        const QPainterPath outer = polygon({ QPointF(-0.72, 0.50), QPointF(0.0, -0.50),
            QPointF(0.72, 0.50) });
        const QPainterPath inner = polygon({ QPointF(-0.35, 0.24), QPointF(0.0, -0.24),
            QPointF(0.35, 0.24) });
        return hollowed(outer, inner, filled);
    }
    case AikenShape::Re: {
        QPainterPath outer;
        outer.moveTo(-0.72, -0.50);
        outer.lineTo(0.72, -0.50);
        outer.lineTo(0.72, -0.20);
        outer.cubicTo(0.72, 0.73, -0.72, 0.73, -0.72, -0.20);
        outer.closeSubpath();
        QPainterPath inner;
        inner.moveTo(-0.56, -0.20);
        inner.lineTo(0.56, -0.20);
        inner.cubicTo(0.52, 0.38, -0.52, 0.38, -0.56, -0.20);
        inner.closeSubpath();
        return hollowed(outer, inner, filled);
    }
    case AikenShape::Mi: {
        const QPainterPath outer = polygon({ QPointF(-0.72, 0.0), QPointF(0.0, -0.50),
            QPointF(0.72, 0.0), QPointF(0.0, 0.50) });
        // Aiken's mirrored-mi cavity is a diagonal lozenge, rather than a
        // smaller concentric diamond.
        const QPainterPath inner = polygon({ QPointF(-0.36, 0.08), QPointF(0.12, -0.25),
            QPointF(0.40, -0.06), QPointF(-0.08, 0.27) });
        return hollowed(outer, inner, filled);
    }
    case AikenShape::Fa: {
        const QPainterPath outer = stemUp
            ? polygon({ QPointF(-0.72, -0.50), QPointF(0.72, -0.50), QPointF(0.72, 0.50) })
            : polygon({ QPointF(-0.72, -0.50), QPointF(-0.72, 0.50), QPointF(0.72, 0.50) });
        const QPainterPath inner = stemUp
            ? polygon({ QPointF(-0.22, -0.25), QPointF(0.48, -0.25), QPointF(0.48, 0.24) })
            : polygon({ QPointF(-0.48, -0.24), QPointF(-0.48, 0.25), QPointF(0.22, 0.25) });
        return hollowed(outer, inner, filled);
    }
    case AikenShape::Sol: {
        const QPainterPath outer = tiltedOval(1.42, 0.98, -22);
        const QPainterPath inner = tiltedOval(0.92, 0.46, -22);
        return hollowed(outer, inner, filled);
    }
    case AikenShape::La: {
        QPainterPath outer;
        outer.addRoundedRect(QRectF(-0.72, -0.50, 1.44, 1.0), 0.06, 0.06);
        QPainterPath inner;
        inner.addRect(QRectF(-0.56, -0.22, 1.12, 0.44));
        return hollowed(outer, inner, filled);
    }
    case AikenShape::Ti: {
        QPainterPath outer;
        outer.moveTo(0.0, 0.50);
        outer.lineTo(-0.70, -0.10);
        outer.cubicTo(-0.34, -0.63, 0.34, -0.63, 0.70, -0.10);
        outer.closeSubpath();
        QPainterPath inner;
        inner.moveTo(0.0, 0.22);
        inner.lineTo(-0.42, -0.08);
        inner.cubicTo(-0.22, -0.34, 0.22, -0.34, 0.42, -0.08);
        inner.closeSubpath();
        return hollowed(outer, inner, filled);
    }
    }
    return notehead(filled);
}

QPainterPath notehead(bool filled)
{
    QPainterPath head = tiltedOval(1.35, 0.98, -22);
    if (filled)
        return head;
    QPainterPath inner = tiltedOval(0.78, 0.42, -22);
    return head.subtracted(inner);
}

QPainterPath wholeNotehead()
{
    QPainterPath head = tiltedOval(1.7, 1.0, 0);
    QPainterPath inner = tiltedOval(1.0, 0.45, -28);
    return head.subtracted(inner);
}

QPainterPath trebleClef()
{
    // Built as a single stroke: the tail below the staff, up through the spiral
    // crossing the G line, over the loop, and down into the eye.
    QPainterPath path;
    path.moveTo(0.05, 2.55);
    path.cubicTo(0.95, 2.35, 1.05, 1.35, 0.35, 0.95);
    path.cubicTo(-0.55, 0.50, -1.15, 1.35, -0.55, 2.05);
    path.cubicTo(-0.05, 2.65, 0.95, 2.55, 1.25, 1.55);
    path.cubicTo(1.65, 0.25, 0.75, -0.85, 0.15, -1.85);
    path.cubicTo(-0.35, -2.75, -0.25, -3.85, 0.45, -4.25);
    path.cubicTo(1.05, -3.65, 1.15, -2.35, 0.55, -1.35);
    path.cubicTo(-0.15, -0.15, -1.35, 0.95, -1.35, 2.15);
    path.cubicTo(-1.35, 3.15, -0.55, 3.75, 0.25, 3.65);
    path.cubicTo(0.95, 3.55, 1.25, 2.95, 0.95, 2.45);

    QPainterPathStroker stroker;
    stroker.setWidth(0.28);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath outline = stroker.createStroke(path);

    QPainterPath eye;
    eye.addEllipse(QPointF(0.05, 2.62), 0.24, 0.24);
    outline.addPath(eye);
    return outline;
}

QPainterPath bassClef()
{
    QPainterPath path;
    path.moveTo(-0.95, -1.05);
    path.cubicTo(-0.15, -1.45, 0.85, -1.05, 0.85, -0.15);
    path.cubicTo(0.85, 1.05, -0.25, 1.85, -1.15, 2.15);

    QPainterPathStroker stroker;
    stroker.setWidth(0.34);
    stroker.setCapStyle(Qt::RoundCap);
    QPainterPath outline = stroker.createStroke(path);

    QPainterPath head;
    head.addEllipse(QPointF(-0.95, -0.95), 0.30, 0.30);
    outline.addPath(head);

    // The two dots that name the F line.
    QPainterPath dots;
    dots.addEllipse(QPointF(1.25, -0.5), 0.15, 0.15);
    dots.addEllipse(QPointF(1.25, 0.5), 0.15, 0.15);
    outline.addPath(dots);
    return outline;
}

QPainterPath sharp()
{
    QPainterPath path;
    // Two near-vertical strokes and two thicker slanted crossbars.
    path.addRect(QRectF(-0.30, -1.05, 0.11, 2.10));
    path.addRect(QRectF(0.19, -1.25, 0.11, 2.10));
    QPainterPath barA;
    barA.addRect(QRectF(-0.52, -0.34, 1.04, 0.24));
    path.addPath(QTransform().rotate(-11).map(barA));
    QPainterPath barB;
    barB.addRect(QRectF(-0.52, 0.36, 1.04, 0.24));
    path.addPath(QTransform().rotate(-11).map(barB));
    return path.simplified();
}

QPainterPath flat()
{
    QPainterPath stem;
    stem.addRect(QRectF(-0.34, -1.45, 0.12, 2.25));
    QPainterPath bowl;
    bowl.moveTo(-0.22, 0.80);
    bowl.cubicTo(0.45, 0.45, 0.55, -0.35, -0.22, -0.10);
    bowl.closeSubpath();
    QPainterPathStroker stroker;
    stroker.setWidth(0.16);
    QPainterPath outline = stroker.createStroke(bowl);
    outline.addPath(stem);
    return outline.simplified();
}

QPainterPath natural()
{
    QPainterPath path;
    path.addRect(QRectF(-0.28, -1.15, 0.10, 1.90));
    path.addRect(QRectF(0.18, -0.75, 0.10, 1.90));
    QPainterPath barA;
    barA.addRect(QRectF(-0.28, -0.40, 0.56, 0.22));
    path.addPath(QTransform().rotate(-9).map(barA));
    QPainterPath barB;
    barB.addRect(QRectF(-0.28, 0.28, 0.56, 0.22));
    path.addPath(QTransform().rotate(-9).map(barB));
    return path.simplified();
}

QPainterPath doubleSharp()
{
    QPainterPath path;
    // The squat saltire of a double sharp.
    QPainterPath arm;
    arm.addRect(QRectF(-0.42, -0.13, 0.84, 0.26));
    path.addPath(QTransform().rotate(45).map(arm));
    path.addPath(QTransform().rotate(-45).map(arm));
    return path.simplified();
}

QPainterPath doubleFlat()
{
    QPainterPath path = flat();
    QPainterPath second = QTransform::fromTranslate(0.62, 0).map(flat());
    path.addPath(second);
    return path;
}

QPainterPath rest(int durationBase)
{
    QPainterPath path;
    switch (durationBase) {
    case 1:
        // Hangs from the fourth line.
        path.addRect(QRectF(-0.55, -1.0, 1.10, 0.50));
        return path;
    case 2:
        // Sits on the third line.
        path.addRect(QRectF(-0.55, 0.0, 1.10, 0.50));
        return path;
    case 4: {
        QPainterPath stroke;
        stroke.moveTo(-0.30, -1.05);
        stroke.cubicTo(0.25, -0.55, -0.30, -0.25, 0.20, 0.20);
        stroke.cubicTo(-0.35, 0.05, -0.35, 0.75, 0.30, 1.05);
        stroke.cubicTo(-0.20, 0.85, -0.45, 1.15, -0.05, 1.45);
        QPainterPathStroker stroker;
        stroker.setWidth(0.24);
        stroker.setCapStyle(Qt::RoundCap);
        return stroker.createStroke(stroke);
    }
    default: {
        // Eighth and shorter: a diagonal stem with one hook per flag.
        const int hooks = durationBase == 8 ? 1 : durationBase == 16 ? 2 : durationBase == 32 ? 3 : 4;
        QPainterPath stem;
        stem.moveTo(0.35, -0.85);
        stem.lineTo(-0.15, 0.95 + 0.25 * (hooks - 1));
        QPainterPathStroker stroker;
        stroker.setWidth(0.14);
        QPainterPath outline = stroker.createStroke(stem);
        for (int i = 0; i < hooks; ++i) {
            const qreal y = -0.75 + i * 0.55;
            QPainterPath hook;
            hook.addEllipse(QPointF(0.22, y), 0.20, 0.16);
            outline.addPath(hook);
            QPainterPath tail;
            tail.moveTo(0.22, y);
            tail.cubicTo(-0.05, y - 0.20, -0.30, y - 0.10, -0.38, y + 0.10);
            QPainterPathStroker tailStroker;
            tailStroker.setWidth(0.12);
            outline.addPath(tailStroker.createStroke(tail));
        }
        return outline.simplified();
    }
    }
}

QPainterPath flag(int count, bool stemUp)
{
    QPainterPath outline;
    for (int i = 0; i < count; ++i) {
        const qreal offset = i * 0.72;
        QPainterPath hook;
        hook.moveTo(0.0, offset);
        hook.cubicTo(0.55, offset + 0.35, 0.70, offset + 0.95, 0.35, offset + 1.55);
        QPainterPathStroker stroker;
        stroker.setWidth(0.26);
        stroker.setCapStyle(Qt::RoundCap);
        outline.addPath(stroker.createStroke(hook));
    }
    return stemUp ? outline : QTransform().scale(1, -1).map(outline);
}

QPainterPath fermata(bool above)
{
    QPainterPath arc;
    arc.moveTo(-0.95, 0.0);
    arc.cubicTo(-0.85, -1.10, 0.85, -1.10, 0.95, 0.0);
    QPainterPathStroker stroker;
    stroker.setWidth(0.18);
    stroker.setCapStyle(Qt::RoundCap);
    QPainterPath outline = stroker.createStroke(arc);
    QPainterPath dot;
    dot.addEllipse(QPointF(0.0, -0.30), 0.17, 0.17);
    outline.addPath(dot);
    return above ? outline : QTransform().scale(1, -1).map(outline);
}

} // namespace ope::ui::glyphs
