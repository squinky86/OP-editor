// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "cli/Cli.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace ope::cli;

class CliTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
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
};

QTEST_APPLESS_MAIN(CliTests)
#include "CliTests.moc"
