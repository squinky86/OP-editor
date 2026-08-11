// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "core/CorpusArchive.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <zip.h>

using namespace ope::corpus;

namespace {

bool makeZip(const QString &path, const QList<QPair<QByteArray, QByteArray>> &entries)
{
    int error = 0;
    zip_t *archive = zip_open(QFile::encodeName(path).constData(), ZIP_CREATE | ZIP_TRUNCATE,
        &error);
    if (!archive)
        return false;
    for (const auto &[name, contents] : entries) {
        zip_source_t *source
            = zip_source_buffer(archive, contents.constData(), contents.size(), 0);
        if (!source || zip_file_add(archive, name.constData(), source, ZIP_FL_ENC_UTF_8) < 0) {
            if (source)
                zip_source_free(source);
            zip_discard(archive);
            return false;
        }
    }
    return zip_close(archive) == 0;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class CorpusArchiveTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void extractsOneRootAndStripsItsName()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString archive = temporary.filePath(QStringLiteral("songs.zip"));
        QVERIFY(makeZip(archive,
            { { QByteArray("OP-songs-main/1/song.toml"), QByteArray("title = \"One\"\n") },
                { QByteArray("OP-songs-main/docs/guide.md"), QByteArray("guide\n") } }));

        const QString output = temporary.filePath(QStringLiteral("out"));
        const auto extracted = extractSnapshotZip(archive, output);
        const QString extractError = extracted ? QString() : extracted.error();
        QVERIFY2(extracted, qPrintable(extractError));
        QCOMPARE(extracted->archiveRoot, QStringLiteral("OP-songs-main"));
        QCOMPARE(extracted->files, 2);
        QCOMPARE(readAll(QDir(output).filePath(QStringLiteral("1/song.toml"))),
            QByteArray("title = \"One\"\n"));
        QVERIFY(!QFileInfo::exists(QDir(output).filePath(QStringLiteral("OP-songs-main"))));
    }

    void rejectsTraversalBeforeWritingOutsideTheDestination()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString archive = temporary.filePath(QStringLiteral("unsafe.zip"));
        QVERIFY(makeZip(archive,
            { { QByteArray("OP-songs-main/../../escaped"), QByteArray("nope") } }));

        const QString output = temporary.filePath(QStringLiteral("out"));
        const auto extracted = extractSnapshotZip(archive, output);
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("unsafe archive path")));
        QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("escaped"))));
    }

    void replacementRetainsThePreviousDirectoryAsABackup()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QDir root(temporary.path());
        QVERIFY(root.mkpath(QStringLiteral("staged/1")));
        QVERIFY(root.mkpath(QStringLiteral("installed/1")));
        QFile staged(root.filePath(QStringLiteral("staged/1/song.toml")));
        QVERIFY(staged.open(QIODevice::WriteOnly));
        staged.write("new\n");
        staged.close();
        QFile installed(root.filePath(QStringLiteral("installed/1/song.toml")));
        QVERIFY(installed.open(QIODevice::WriteOnly));
        installed.write("old\n");
        installed.close();

        const auto result = installValidatedSnapshot(root.filePath(QStringLiteral("staged")),
            root.filePath(QStringLiteral("installed")));
        const QString installError = result ? QString() : result.error();
        QVERIFY2(result, qPrintable(installError));
        QVERIFY(!result->backup.isEmpty());
        QCOMPARE(readAll(root.filePath(QStringLiteral("installed/1/song.toml"))),
            QByteArray("new\n"));
        QCOMPARE(readAll(QDir(result->backup).filePath(QStringLiteral("1/song.toml"))),
            QByteArray("old\n"));
    }

    void extractsARealSnapshotWhenProvided()
    {
        const QString archive = QString::fromLocal8Bit(qgetenv("OPE_SNAPSHOT_ZIP"));
        if (archive.isEmpty())
            QSKIP("Set OPE_SNAPSHOT_ZIP to exercise a downloaded GitHub archive.");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto extracted
            = extractSnapshotZip(archive, temporary.filePath(QStringLiteral("out")));
        const QString extractError = extracted ? QString() : extracted.error();
        QVERIFY2(extracted, qPrintable(extractError));
        QVERIFY(extracted->files > 100);
        QVERIFY(QFileInfo::exists(temporary.filePath(QStringLiteral("out/1/song.toml"))));
    }
};

QTEST_APPLESS_MAIN(CorpusArchiveTests)
#include "CorpusArchiveTests.moc"
