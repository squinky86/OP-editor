// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>
#include "notation/Duration.hpp"

using namespace OpenPsalm;

class DurationTests : public QObject {
    Q_OBJECT

private slots:
    void testStandardDurations() {
        Duration whole(1);
        QCOMPARE(whole.toInternalTicks(), 192);

        Duration half(2);
        QCOMPARE(half.toInternalTicks(), 96);

        Duration quarter(4);
        QCOMPARE(quarter.toInternalTicks(), 48);

        Duration eighth(8);
        QCOMPARE(eighth.toInternalTicks(), 24);

        Duration sixteenth(16);
        QCOMPARE(sixteenth.toInternalTicks(), 12);
    }

    void testDottedDurations() {
        Duration dottedQuarter(4, 1);
        QCOMPARE(dottedQuarter.toInternalTicks(), 72); // 48 + 24

        Duration doubleDottedHalf(2, 2);
        QCOMPARE(doubleDottedHalf.toInternalTicks(), 168); // 96 + 48 + 24
    }

    void testTupletDurations() {
        Duration tripletQuarter(4, 0, 3, 2);
        QCOMPARE(tripletQuarter.toInternalTicks(), 32); // (48 * 2) / 3

        Duration tripletEighth(8, 0, 3, 2);
        QCOMPARE(tripletEighth.toInternalTicks(), 16); // (24 * 2) / 3
    }

    void testTickConversion() {
        // 48 internal ticks -> 16 phrase ticks (scale factor 3)
        QCOMPARE(Ticks::internalToPhrase(48), 16);
        QCOMPARE(Ticks::phraseToInternal(16), 48);
    }
};

QTEST_MAIN(DurationTests)
#include "DurationTests.moc"
