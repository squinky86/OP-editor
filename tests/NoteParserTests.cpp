// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>
#include "notation/NoteParser.hpp"
#include "notation/LyricAligner.hpp"

using namespace OpenPsalm;

class NoteParserTests : public QObject {
    Q_OBJECT

private slots:
    void testBasicMeasureParsing() {
        QString notes = QStringLiteral("c'4 d'4 e'4 f'4 | g'2 g'2 |");
        auto res = NoteParser::parse(notes);
        QVERIFY(!res.hasErrors);
        QCOMPARE(res.measures.size(), 2UL);
        QCOMPARE(res.measures[0].events.size(), 4UL);
        QCOMPARE(res.measures[1].events.size(), 2UL);
    }

    void testChordsRestsAndSpacers() {
        QString notes = QStringLiteral("<c' e' g'>4 r4 s4 a'4 |");
        auto res = NoteParser::parse(notes);
        QVERIFY(!res.hasErrors);
        QCOMPARE(res.measures.size(), 1UL);
        QCOMPARE(res.measures[0].events.size(), 4UL);

        const auto& ev0 = res.measures[0].events[0];
        QCOMPARE(ev0.kind, NoteKind::Chord);
        QCOMPARE(ev0.pitches.size(), 3UL);

        const auto& ev1 = res.measures[0].events[1];
        QCOMPARE(ev1.kind, NoteKind::Rest);

        const auto& ev2 = res.measures[0].events[2];
        QCOMPARE(ev2.kind, NoteKind::Spacer);
    }

    void testTupletParsing() {
        QString notes = QStringLiteral("{3:2 c'4 d'4 e'4} f'2 |");
        auto res = NoteParser::parse(notes);
        QVERIFY(!res.hasErrors);
        QCOMPARE(res.measures.size(), 1UL);
        QCOMPARE(res.measures[0].events.size(), 4UL);

        // First 3 notes should have tuplet ratio 3:2
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(res.measures[0].events[i].duration.tupletN, 3);
            QCOMPARE(res.measures[0].events[i].duration.tupletM, 2);
            QCOMPARE(res.measures[0].events[i].duration.toInternalTicks(), 32);
        }
        // Total ticks in measure: 32*3 + 96 = 192 (4/4 measure)
        QCOMPARE(res.measures[0].totalInternalTicks(), 192);
    }

    void testSectionMarkersAndSlurs() {
        QString notes = QStringLiteral("@c (c'4 d'4) e'4~ e'4 |");
        auto res = NoteParser::parse(notes);
        QVERIFY(!res.hasErrors);

        const auto& ev0 = res.measures[0].events[0];
        QCOMPARE(ev0.sectionMarker, SectionMarker::Chorus);
        QVERIFY(ev0.slurStart);

        const auto& ev1 = res.measures[0].events[1];
        QVERIFY(ev1.slurEnd);

        const auto& ev2 = res.measures[0].events[2];
        QVERIFY(ev2.tie);
    }
};

QTEST_MAIN(NoteParserTests)
#include "NoteParserTests.moc"
