// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "cli/Cli.h"
#include "Fixtures.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace ope::cli;

class CliTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void ttbbCorpusAndOverlayUseTheSharedValidator()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        QVERIFY(root.mkpath("369"));
        const auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(root.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        };
        write("369/song.toml", ope::fixtures::ttbbSong());
        Options options;
        options.root = dir.path();
        options.quiet = true;
        const auto valid = check(options);
        QVERIFY(valid.passed());
        QCOMPARE(valid.files, 1);
        QCOMPARE(valid.warnings, 0);
        write("369/song_es.toml", "title = \"Translated\"\n[parts.Tenor1]\nclef = \"alto\"\n");
        options.root = root.filePath("369/song_es.toml");
        const auto conflict = check(options);
        QCOMPARE(conflict.errors, 1);
        QVERIFY(!conflict.passed());
        QCOMPARE(run(options), 1);
        write("369/song_es.toml", "title = \"Translated\"\n[parts.Tenor1]\nclef = \" TENOR \"\n");
        QVERIFY(check(options).passed());
    }

    void parsesQuietAndPositiveLimit()
    {
        Options options;
        int exitCode = -1;
        QVERIFY(parse({ QStringLiteral("--check"), QStringLiteral("songs"),
                          QStringLiteral("--quiet"), QStringLiteral("--limit"),
                          QStringLiteral("12") },
            options, exitCode));
        QCOMPARE(exitCode, 0);
        QCOMPARE(options.root, QStringLiteral("songs"));
        QVERIFY(options.quiet);
        QCOMPARE(options.limit, 12);
    }

    void rejectsMissingLimit()
    {
        Options options;
        int exitCode = 0;
        QVERIFY(!parse({ QStringLiteral("--limit") }, options, exitCode));
        QCOMPARE(exitCode, 2);
    }

    void rejectsInvalidLimit_data()
    {
        QTest::addColumn<QString>("value");
        QTest::newRow("zero") << QStringLiteral("0");
        QTest::newRow("negative") << QStringLiteral("-2");
        QTest::newRow("not a number") << QStringLiteral("many");
        QTest::newRow("trailing text") << QStringLiteral("2songs");
    }

    void rejectsInvalidLimit()
    {
        QFETCH(QString, value);
        Options options;
        int exitCode = 0;
        QVERIFY(!parse({ QStringLiteral("--limit"), value }, options, exitCode));
        QCOMPARE(exitCode, 2);
    }

    void aSingleOverlayIsValidatedWithItsSiblingBase()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        QFile base(root.filePath(QStringLiteral("song.toml")));
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("title = \"Base\"\n"
                   "time_sig_numerator = 4\n"
                   "time_sig_denominator = 4\n"
                   "tempo_bpm = 100\n"
                   "verse_count = 1\n"
                   "[parts.Soprano]\n"
                   "notes = \"c4 d4 e4 f4\"\n"
                   "[lyrics.1]\n"
                   "text = \"one two three four\"\n");
        base.close();
        const QString overlayPath = root.filePath(QStringLiteral("song_es.toml"));
        QFile overlay(overlayPath);
        QVERIFY(overlay.open(QIODevice::WriteOnly));
        overlay.write("title = \"Traducción\"\n");
        overlay.close();

        Options options;
        options.root = overlayPath;
        options.quiet = true;
        QCOMPARE(run(options), 0);
    }

    void exposesTheFullCheckAsStructuredData()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("1")));
        QFile base(root.filePath(QStringLiteral("1/song.toml")));
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("title = \"Checked\"\n"
                   "time_sig_numerator = 4\n"
                   "time_sig_denominator = 4\n"
                   "tempo_bpm = 100\n"
                   "verse_count = 1\n"
                   "[parts.Soprano]\n"
                   "choral_type = \"soprano\"\n"
                   "clef = \"treble\"\n"
                   "notes = \"c4 d4 e4 f4\"\n"
                   "[lyrics.1]\n"
                   "text = \"one two three four\"\n");
        base.close();

        Options options;
        options.root = root.path();
        const CheckSummary summary = check(options);
        QVERIFY2(summary.passed(), qPrintable(summary.description()));
        QCOMPARE(summary.files, 1);
        QCOMPARE(summary.parseFailures, 0);
        QCOMPARE(summary.roundTripFailures, 0);
        QCOMPARE(summary.reemitFailures, 0);
        QCOMPARE(summary.errors, 0);
    }

    void structuredCheckReportsProgressAndCancelsBetweenFiles()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QByteArray song(
            "title = \"Checked\"\n"
            "time_sig_numerator = 4\n"
            "time_sig_denominator = 4\n"
            "tempo_bpm = 100\n"
            "verse_count = 1\n"
            "[parts.Soprano]\n"
            "choral_type = \"soprano\"\n"
            "clef = \"treble\"\n"
            "notes = \"c4 d4 e4 f4\"\n"
            "[lyrics.1]\n"
            "text = \"one two three four\"\n");
        for (int id = 1; id <= 2; ++id) {
            QVERIFY(root.mkpath(QString::number(id)));
            QFile file(root.filePath(QStringLiteral("%1/song.toml").arg(id)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(song), song.size());
        }

        bool cancel = false;
        int reportedTotal = 0;
        Options options;
        options.root = root.path();
        options.cancelled = [&cancel] { return cancel; };
        options.progress = [&cancel, &reportedTotal](int completed, int total, const QString &) {
            reportedTotal = total;
            if (completed == 1)
                cancel = true;
        };
        const CheckSummary summary = check(options);
        QVERIFY(summary.cancelled);
        QVERIFY(!summary.passed());
        QCOMPARE(summary.files, 1);
        QCOMPARE(reportedTotal, 2);
        QVERIFY(summary.description().contains(QStringLiteral("cancelled")));
    }
};

QTEST_APPLESS_MAIN(CliTests)
#include "CliTests.moc"
