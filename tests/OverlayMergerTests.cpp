// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>
#include "format/TomlLoader.hpp"
#include "format/OverlayMerger.hpp"

using namespace OpenPsalm;

class OverlayMergerTests : public QObject {
    Q_OBJECT

private slots:
    void testTranslationOverlayMerging() {
        QString baseToml = QStringLiteral(
            "title = \"Base Title\"\n"
            "verse_count = 3\n"
            "language = \"en\"\n"
            "key_signature = \"C\"\n"
            "time_sig_numerator = 4\n"
            "time_sig_denominator = 4\n"
            "tempo_bpm = 100\n"
            "\n"
            "[parts.Soprano]\n"
            "choral_type = \"Soprano\"\n"
            "clef = \"treble\"\n"
            "staff_number = 1\n"
            "notes = \"c'4 d'4 e'4 f'4 | g'1 |\"\n"
            "\n"
            "[lyrics.1]\n"
            "text = \"One two three four five\"\n"
        );

        QString overlayToml = QStringLiteral(
            "title = \"Titulo Traducido\"\n"
            "language = \"es\"\n"
            "\n"
            "[lyrics.1]\n"
            "text = \"Uno dos tres cua -- tro\"\n"
        );

        auto baseRes = TomlLoader::loadFromString(baseToml, QStringLiteral("song.toml"));
        auto overlayRes = TomlLoader::loadFromString(overlayToml, QStringLiteral("song_es.toml"));

        SongData merged = OverlayMerger::merge(baseRes.songData, overlayRes.songData);

        // Overridden
        QCOMPARE(merged.title, QStringLiteral("Titulo Traducido"));
        QCOMPARE(merged.language, QStringLiteral("es"));
        QCOMPARE(merged.lyrics.section(QStringLiteral("1")), QStringLiteral("Uno dos tres cua -- tro"));

        // Inherited
        QCOMPARE(merged.verseCount, 3);
        QCOMPARE(merged.keySignature, QStringLiteral("C"));
        QCOMPARE(merged.timeSigNumerator, 4);
        QCOMPARE(merged.tempoBpm, 100);
        QVERIFY(merged.parts.contains(QStringLiteral("Soprano")));
        QCOMPARE(merged.parts[QStringLiteral("Soprano")].parsedMeasures.size(), 2UL);
    }
};

QTEST_MAIN(OverlayMergerTests)
#include "OverlayMergerTests.moc"
