// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>
#include "validation/Validator.hpp"
#include "notation/NoteParser.hpp"

using namespace OpenPsalm;

class ValidatorTests : public QObject {
    Q_OBJECT

private slots:
    void testDurationMismatchDetection() {
        SongData song;
        song.title = QStringLiteral("Test Validation");
        song.verseCount = 1;
        song.keySignature = QStringLiteral("C");
        song.timeSigNumerator = 4;
        song.timeSigDenominator = 4; // 192 ticks per measure
        song.tempoBpm = 100;

        PartData p;
        p.name = QStringLiteral("Soprano");
        p.notesText = QStringLiteral("c'1 | c'4 d'4 e'4 |"); // Measure 2 has only 3 quarters (144 ticks) in a 4/4 measure!
        auto parseRes = NoteParser::parse(p.notesText);
        p.parsedMeasures = parseRes.measures;
        song.parts.insert(p.name, p);

        auto diags = Validator::validateSong(song);
        bool foundDurationErr = false;
        for (const auto& d : diags) {
            if (d.code == QStringLiteral("MEASURE_DURATION_MISMATCH")) {
                foundDurationErr = true;
                break;
            }
        }
        QVERIFY(foundDurationErr);
    }

    void testMeasureCountMismatchDetection() {
        SongData song;
        song.title = QStringLiteral("Mismatch Song");
        song.verseCount = 1;
        song.keySignature = QStringLiteral("C");
        song.timeSigNumerator = 4;
        song.timeSigDenominator = 4;
        song.tempoBpm = 100;

        PartData sPart;
        sPart.name = QStringLiteral("Soprano");
        sPart.notesText = QStringLiteral("c'1 | d'1 |"); // 2 measures
        sPart.parsedMeasures = NoteParser::parse(sPart.notesText).measures;
        song.parts.insert(sPart.name, sPart);

        PartData aPart;
        aPart.name = QStringLiteral("Alto");
        aPart.notesText = QStringLiteral("c'1 |"); // 1 measure
        aPart.parsedMeasures = NoteParser::parse(aPart.notesText).measures;
        song.parts.insert(aPart.name, aPart);

        auto diags = Validator::validateSong(song);
        bool foundMismatch = false;
        for (const auto& d : diags) {
            if (d.code == QStringLiteral("MEASURE_COUNT_MISMATCH")) {
                foundMismatch = true;
                break;
            }
        }
        QVERIFY(foundMismatch);
    }
};

QTEST_MAIN(ValidatorTests)
#include "ValidatorTests.moc"
