// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
#include "Fixtures.h"
#include "core/Clefs.h"
#include "core/Voicing.h"
#include "core/Validator.h"
#include <QTest>

using namespace ope;

class MetadataTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void clefGeometry_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("anchor");
        QTest::addColumn<int>("line");
        QTest::addColumn<int>("octave");
        QTest::newRow("treble") << QStringLiteral("treble") << 30 << 2 << 0;
        QTest::newRow("bass") << QStringLiteral("bass") << 18 << 4 << 0;
        QTest::newRow("octave treble") << QStringLiteral("treble_8") << 23 << 2 << -1;
        QTest::newRow("alto C") << QStringLiteral("alto") << 24 << 3 << 0;
        QTest::newRow("tenor C") << QStringLiteral("tenor") << 22 << 4 << 0;
    }

    void clefGeometry()
    {
        QFETCH(QString, name);
        QFETCH(int, anchor);
        QFETCH(int, line);
        QFETCH(int, octave);
        const auto clef = clefs::parse(QStringLiteral("  %1  ").arg(name.toUpper()));
        QVERIFY(clef);
        QCOMPARE(clef->name, name);
        QCOMPARE(clef->bottomLineDiatonic, anchor);
        QCOMPARE(clef->line, line);
        QCOMPARE(clef->octaveChange, octave);
        if (name == "alto" || name == "tenor") {
            QCOMPARE(clef->sign, clefs::Sign::C);
            QCOMPARE(clef->bottomLineDiatonic + (line - 1) * 2, 28); // C4
        }
    }

    void onlyOmissionDefaultsToTreble()
    {
        QCOMPARE(clefs::effective(std::nullopt)->name, QStringLiteral("treble"));
        for (const QString &value : { QString(), QStringLiteral("C"), QStringLiteral("baritone"), QStringLiteral("soprano") })
            QVERIFY(!clefs::effective(value));
        QCOMPARE(validClefs().size(), 5);
        QCOMPARE(validChoralTypes().size(), 7);
        QVERIFY(!voicing::role(u"tenor I"));
        QCOMPARE(voicing::role(u" TeNoR1 ")->label, QStringLiteral("Tenor I"));
        QCOMPARE(voicing::role(u"tenor2")->stem, voicing::Stem::Down);
        QCOMPARE(voicing::role(u"baritone")->stem, voicing::Stem::Up);
    }

    void rolesOrderArbitraryNamesAndNumericSiblings()
    {
        SongDocument doc;
        const QStringList roles { "bass", "baritone", "bass", "tenor2", "soprano", "alto", "bass", "tenor", "tenor1", "custom", "custom" };
        const QStringList ids { "Bass10", "middle", "Bass2", "upper-low", "wrong-name", "Low", "Bass", "SATB", "lead", "z", "a" };
        for (int i = 0; i < roles.size(); ++i) {
            Part &part = doc.ensurePart(ids.at(i));
            part.choralType.set(roles.at(i));
        }
        QStringList order;
        for (const Part *part : doc.partsInDisplayOrder())
            order.append(part->name);
        QCOMPARE(order, QStringList({ "wrong-name", "lead", "Low", "upper-low", "SATB", "middle", "Bass", "Bass2", "Bass10", "a", "z" }));
        QCOMPARE(voicing::lead(doc)->name, QStringLiteral("wrong-name"));
        QCOMPARE(doc.parts.first().name, QStringLiteral("Bass10"));
        QCOMPARE(voicing::shortLabel(doc, *doc.part(u"Bass2")), QStringLiteral("Bass2"));
    }

    void shuffledTtbbAndSatbStaves()
    {
        const auto doc = io::loadBytes("song.toml", fixtures::ttbbSong());
        QVERIFY(doc);
        const auto staves = voicing::staves(*doc);
        QCOMPARE(staves.size(), 2);
        QCOMPARE(staves.at(0).partIndices, QList<int>({3, 1}));
        QCOMPARE(staves.at(1).partIndices, QList<int>({2, 0}));
        QCOMPARE(staves.at(0).clef->name, QStringLiteral("tenor"));
        for (const auto &staff : staves)
            QCOMPARE(voicing::stems(*doc, staff.partIndices), QList<voicing::Stem>({ voicing::Stem::Up, voicing::Stem::Down }));
        QCOMPARE(voicing::lead(*doc)->name, QStringLiteral("Tenor1"));
        QCOMPARE(voicing::stems(*doc, {1}), QList<voicing::Stem>({voicing::Stem::Free}));

        auto satb = io::loadBytes("song.toml", fixtures::baseSong());
        QVERIFY(satb);
        std::reverse(satb->parts.begin(), satb->parts.end());
        QCOMPARE(voicing::staves(*satb).first().partIndices, QList<int>({1, 0}));
        QCOMPARE(voicing::stems(*satb, {1, 0}), QList<voicing::Stem>({voicing::Stem::Up, voicing::Stem::Down}));
    }

    void omittedStavesNeverAliasNumberedStaves()
    {
        auto doc = io::loadBytes("song.toml", fixtures::ttbbSong());
        QVERIFY(doc);
        doc->parts[0].staffNumber.clear(); // raw i+1 would collide with staff 1
        auto staves = voicing::staves(*doc);
        QCOMPARE(staves.size(), 3);
        QVERIFY(!staves.last().number);
        QCOMPARE(staves.last().partIndices, QList<int>({0}));
        QCOMPARE(countBySeverity(validate(*doc), Severity::Error), 0);
        doc->parts[1].clef.set("alto");
        staves = voicing::staves(*doc);
        QVERIFY(!staves.first().clef);
        QVERIFY(staves.first().problem.contains("Tenor1"));
        QVERIFY(staves.first().problem.contains("Tenor2"));
    }

    void duplicateRolesAndExtraStavesHaveDeterministicStems()
    {
        SongDocument doc;
        for (const QString &name : { QStringLiteral("Bass10"), QStringLiteral("Bass"), QStringLiteral("Bass2") }) {
            Part &part = doc.ensurePart(name);
            part.choralType.set("bass");
            part.clef.set("bass");
            part.staffNumber.set(3);
        }
        const auto group = voicing::staves(doc).first();
        QCOMPARE(group.partIndices, QList<int>({1, 2, 0}));
        QCOMPARE(voicing::stems(doc, group.partIndices),
            QList<voicing::Stem>({ voicing::Stem::Down, voicing::Stem::Up, voicing::Stem::Up }));
        doc.parts[0].staffNumber.set(4);
        const auto groups = voicing::staves(doc);
        QCOMPARE(groups.size(), 2);
        QCOMPARE(voicing::stems(doc, groups[1].partIndices), QList<voicing::Stem>({voicing::Stem::Free}));
    }
};

QTEST_APPLESS_MAIN(MetadataTests)
#include "MetadataTests.moc"
