// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>
#include "format/TomlLoader.hpp"
#include "format/TomlSerializer.hpp"

using namespace OpenPsalm;

class TomlLoaderTests : public QObject {
    Q_OBJECT

private slots:
    void testLoadCompleteSong() {
        QString toml = QStringLiteral(
            "title = \"The Lord's My Shepherd\"\n"
            "verse_count = 5\n"
            "key_signature = \"F\"\n"
            "time_sig_numerator = 3\n"
            "time_sig_denominator = 4\n"
            "tempo_bpm = 96\n"
            "phrase_breaks = [\"2:0\", \"4:0\"]\n"
            "\n"
            "[parts.Soprano]\n"
            "choral_type = \"Soprano\"\n"
            "clef = \"treble\"\n"
            "staff_number = 1\n"
            "notes = '''\n"
            "f'4 a'4 c''4 | c''2. | f'4 a'4 c''4 | c''2. |\n"
            "'''\n"
            "\n"
            "[lyrics.1]\n"
            "text = \"The Lord's my shep -- herd I'll not want\"\n"
        );

        auto res = TomlLoader::loadFromString(toml);
        QVERIFY(res.success);
        QCOMPARE(res.songData.title, QStringLiteral("The Lord's My Shepherd"));
        QCOMPARE(res.songData.verseCount, 5);
        QCOMPARE(res.songData.keySignature, QStringLiteral("F"));
        QCOMPARE(res.songData.timeSigNumerator, 3);
        QCOMPARE(res.songData.timeSigDenominator, 4);
        QCOMPARE(res.songData.tempoBpm, 96);
        QCOMPARE(res.songData.phraseBreaks.size(), 2UL);

        QVERIFY(res.songData.parts.contains(QStringLiteral("Soprano")));
        const auto& part = res.songData.parts[QStringLiteral("Soprano")];
        QCOMPARE(part.parsedMeasures.size(), 4UL);

        QVERIFY(res.songData.lyrics.hasSection(QStringLiteral("1")));
        QCOMPARE(res.songData.lyrics.section(QStringLiteral("1")), QStringLiteral("The Lord's my shep -- herd I'll not want"));
    }

    void testSerializeRoundtrip() {
        SongData song;
        song.title = QStringLiteral("Roundtrip Song");
        song.verseCount = 2;
        song.keySignature = QStringLiteral("G");
        song.timeSigNumerator = 4;
        song.timeSigDenominator = 4;
        song.tempoBpm = 110;

        PartData part;
        part.name = QStringLiteral("Soprano");
        part.choralType = ChoralType::Soprano;
        part.clef = Clef::Treble;
        part.staffNumber = 1;
        part.notesText = QStringLiteral("g'4 a'4 b'4 c''4 | d''1 |");
        song.parts.insert(part.name, part);

        song.lyrics.setSection(QStringLiteral("1"), QStringLiteral("Praise the Lord our God"));

        QString serialized = TomlSerializer::serialize(song);
        auto res = TomlLoader::loadFromString(serialized);
        QVERIFY(res.success);
        QCOMPARE(res.songData.title, song.title);
        QCOMPARE(res.songData.verseCount, song.verseCount);
        QCOMPARE(res.songData.tempoBpm, song.tempoBpm);
        QVERIFY(res.songData.parts.contains(QStringLiteral("Soprano")));
        QCOMPARE(res.songData.lyrics.section(QStringLiteral("1")), QStringLiteral("Praise the Lord our God"));
    }
};

QTEST_MAIN(TomlLoaderTests)
#include "TomlLoaderTests.moc"
