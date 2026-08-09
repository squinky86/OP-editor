// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>
#include "notation/Pitch.hpp"

using namespace OpenPsalm;

class PitchTests : public QObject {
    Q_OBJECT

private slots:
    void testPitchParsing() {
        auto c4 = Pitch::fromString(QStringLiteral("c'"));
        QVERIFY(c4.has_value());
        QCOMPARE(c4->step, 'c');
        QCOMPARE(c4->accidental, Accidental::Natural);
        QCOMPARE(c4->octave, 4);
        QCOMPARE(c4->midiNumber(), 60);

        auto fs5 = Pitch::fromString(QStringLiteral("fis'"));
        QVERIFY(fs5.has_value());
        QCOMPARE(fs5->step, 'f');
        QCOMPARE(fs5->accidental, Accidental::Sharp);
        QCOMPARE(fs5->octave, 4);
        QCOMPARE(fs5->midiNumber(), 66);

        auto bb3 = Pitch::fromString(QStringLiteral("bes"));
        QVERIFY(bb3.has_value());
        QCOMPARE(bb3->step, 'b');
        QCOMPARE(bb3->accidental, Accidental::Flat);
        QCOMPARE(bb3->octave, 3);
        QCOMPARE(bb3->midiNumber(), 58);

        auto c2 = Pitch::fromString(QStringLiteral("c,"));
        QVERIFY(c2.has_value());
        QCOMPARE(c2->octave, 2);
        QCOMPARE(c2->midiNumber(), 36);
    }

    void testPitchRoundtrip() {
        QStringList testPitches = {
            QStringLiteral("c'"), QStringLiteral("d'"), QStringLiteral("ees'"),
            QStringLiteral("fis'"), QStringLiteral("g'"), QStringLiteral("aes'"),
            QStringLiteral("bes"), QStringLiteral("c,"), QStringLiteral("e''")
        };

        for (const QString& str : testPitches) {
            auto p = Pitch::fromString(str);
            QVERIFY2(p.has_value(), qPrintable(QStringLiteral("Failed to parse %1").arg(str)));
            QCOMPARE(p->toString(), str);
        }
    }

    void testTransposition() {
        auto c4 = Pitch::fromString(QStringLiteral("c'")).value();
        auto d4 = c4.transposed(2);
        QCOMPARE(d4.midiNumber(), 62);
    }
};

QTEST_MAIN(PitchTests)
#include "PitchTests.moc"
