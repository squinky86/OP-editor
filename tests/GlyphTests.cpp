// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "ui/Glyphs.h"

#include <QTest>

#include <array>

using namespace ope::ui::glyphs;

class GlyphTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void mapsMajorScaleDegreesToAikenShapes()
    {
        static constexpr std::array steps { u'C', u'D', u'E', u'F', u'G', u'A', u'B' };
        static constexpr std::array shapes { AikenShape::Do, AikenShape::Re, AikenShape::Mi,
            AikenShape::Fa, AikenShape::Sol, AikenShape::La, AikenShape::Ti };

        for (std::size_t i = 0; i < steps.size(); ++i)
            QCOMPARE(aikenShapeForPitch(steps.at(i), u"C"), shapes.at(i));

        QCOMPARE(aikenShapeForPitch(u'F', u"F#"), AikenShape::Do);
        QCOMPARE(aikenShapeForPitch(u'C', u"F#"), AikenShape::Sol);
        QCOMPARE(aikenShapeForPitch(u'B', u"Bb"), AikenShape::Do);
    }

    void minorKeysUseTheRelativeMajorShapeAssignment()
    {
        // A minor is la-based: C remains do and A is la.
        QCOMPARE(aikenShapeForPitch(u'C', u"Am"), AikenShape::Do);
        QCOMPARE(aikenShapeForPitch(u'A', u"Am"), AikenShape::La);
        QCOMPARE(aikenShapeForPitch(u'B', u"Am"), AikenShape::Ti);

        // F-sharp minor uses A major's assignment; accidentals do not alter a
        // pitch letter's shape.
        QCOMPARE(aikenShapeForPitch(u'A', u"F#m"), AikenShape::Do);
        QCOMPARE(aikenShapeForPitch(u'F', u"F#m"), AikenShape::La);
    }

    void providesSevenDistinctFilledAndHollowGlyphs()
    {
        static constexpr std::array shapes { AikenShape::Do, AikenShape::Re, AikenShape::Mi,
            AikenShape::Fa, AikenShape::Sol, AikenShape::La, AikenShape::Ti };

        for (const AikenShape shape : shapes) {
            const QPainterPath filled = aikenNotehead(shape, true);
            const QPainterPath hollow = aikenNotehead(shape, false);
            QVERIFY(!filled.isEmpty());
            QVERIFY(!hollow.isEmpty());
            QVERIFY(filled != hollow);
            QVERIFY(filled.boundingRect().width() >= 1.3);
            QVERIFY(filled.boundingRect().height() >= 0.9);
        }

        for (std::size_t i = 0; i < shapes.size(); ++i) {
            for (std::size_t j = i + 1; j < shapes.size(); ++j)
                QVERIFY(aikenNotehead(shapes.at(i), true) != aikenNotehead(shapes.at(j), true));
        }

        QVERIFY(aikenNotehead(AikenShape::Fa, true, true)
            != aikenNotehead(AikenShape::Fa, true, false));
    }
};

QTEST_APPLESS_MAIN(GlyphTests)
#include "GlyphTests.moc"
