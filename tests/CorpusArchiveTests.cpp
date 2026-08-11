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

bool makeSymlinkZip(const QString &path)
{
    int error = 0;
    zip_t *archive = zip_open(QFile::encodeName(path).constData(), ZIP_CREATE | ZIP_TRUNCATE,
        &error);
    if (!archive)
        return false;
    const QByteArray target("../outside");
    zip_source_t *source
        = zip_source_buffer(archive, target.constData(), target.size(), 0);
    const zip_int64_t index = source
        ? zip_file_add(archive, "OP-songs-main/link", source, ZIP_FL_ENC_UTF_8)
        : -1;
    if (index < 0
        || zip_file_set_external_attributes(archive, static_cast<zip_uint64_t>(index), 0,
               ZIP_OPSYS_UNIX, static_cast<zip_uint32_t>(0120777U << 16))
            != 0) {
        if (source && index < 0)
            zip_source_free(source);
        zip_discard(archive);
        return false;
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

        const QString nestedArchive = temporary.filePath(QStringLiteral("nested.zip"));
        QVERIFY(makeZip(nestedArchive,
            { { QByteArray("OP-songs-main/1/../2/song.toml"), QByteArray("nope") } }));
        const auto nested = extractSnapshotZip(
            nestedArchive, temporary.filePath(QStringLiteral("nested-output")));
        QVERIFY(!nested);
        QVERIFY(nested.error().contains(QStringLiteral("unsafe archive path")));
    }

    void rejectsLinksAndMultipleArchiveRoots()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());

        const QString linkArchive = temporary.filePath(QStringLiteral("link.zip"));
        QVERIFY(makeSymlinkZip(linkArchive));
        auto extracted = extractSnapshotZip(
            linkArchive, temporary.filePath(QStringLiteral("link-output")));
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("link or special file")));

        const QString rootsArchive = temporary.filePath(QStringLiteral("roots.zip"));
        QVERIFY(makeZip(rootsArchive,
            { { QByteArray("OP-songs-main/1/song.toml"), QByteArray("one") },
                { QByteArray("unexpected/2/song.toml"), QByteArray("two") } }));
        extracted = extractSnapshotZip(
            rootsArchive, temporary.filePath(QStringLiteral("roots-output")));
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("more than one top-level")));
    }

    void rejectsTruncatedArchiveAndConfiguredSizeLimits()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString truncated = temporary.filePath(QStringLiteral("truncated.zip"));
        QFile invalid(truncated);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QCOMPARE(invalid.write("PK\x03\x04truncated", 13), 13);
        invalid.close();
        auto extracted = extractSnapshotZip(
            truncated, temporary.filePath(QStringLiteral("truncated-output")));
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("could not open downloaded ZIP")));

        const QString archive = temporary.filePath(QStringLiteral("limits.zip"));
        QVERIFY(makeZip(archive,
            { { QByteArray("OP-songs-main/1/song.toml"), QByteArray("12345678") },
                { QByteArray("OP-songs-main/2/song.toml"), QByteArray("abcdefgh") } }));

        ArchiveLimits limits;
        limits.maxEntries = 1;
        extracted = extractSnapshotZip(
            archive, temporary.filePath(QStringLiteral("entry-output")), limits);
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("unsafe entry count")));

        limits.maxEntries = 10;
        limits.maxFileBytes = 7;
        extracted = extractSnapshotZip(
            archive, temporary.filePath(QStringLiteral("file-output")), limits);
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("entry is too large")));

        limits.maxFileBytes = 8;
        limits.maxExpandedBytes = 12;
        extracted = extractSnapshotZip(
            archive, temporary.filePath(QStringLiteral("total-output")), limits);
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("entry is too large")));
    }

    void refusesADestinationContainingOnlyHiddenData()
    {
        QTemporaryDir temporary;
        const QString archive = temporary.filePath(QStringLiteral("songs.zip"));
        QVERIFY(makeZip(archive,
            { { QByteArray("OP-songs-main/1/song.toml"), QByteArray("song") } }));
        const QString output = temporary.filePath(QStringLiteral("output"));
        const QString hidden = QDir(output).filePath(QStringLiteral(".keep"));
        QVERIFY(QDir().mkpath(output));
        QFile marker(hidden);
        QVERIFY(marker.open(QIODevice::WriteOnly));
        QCOMPARE(marker.write("keep\n"), 5);
        marker.close();

        const auto extracted = extractSnapshotZip(archive, output);
        QVERIFY(!extracted);
        QVERIFY(extracted.error().contains(QStringLiteral("destination is not empty")));
        QCOMPARE(readAll(hidden), QByteArray("keep\n"));
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

    void activationFailureRestoresThePreviousDirectory()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QDir root(temporary.path());
        QVERIFY(root.mkpath(QStringLiteral("staged/1")));
        QVERIFY(root.mkpath(QStringLiteral("installed/1")));
        QFile staged(root.filePath(QStringLiteral("staged/1/song.toml")));
        QVERIFY(staged.open(QIODevice::WriteOnly));
        QCOMPARE(staged.write("new\n"), 4);
        staged.close();
        QFile installed(root.filePath(QStringLiteral("installed/1/song.toml")));
        QVERIFY(installed.open(QIODevice::WriteOnly));
        QCOMPARE(installed.write("old\n"), 4);
        installed.close();

        int renameCalls = 0;
        InstallHooks hooks;
        hooks.renameDirectory = [&renameCalls](
                                    const QString &source, const QString &destination) {
            ++renameCalls;
            if (renameCalls == 2)
                return false;
            return QDir().rename(source, destination);
        };
        const auto result = installValidatedSnapshot(root.filePath(QStringLiteral("staged")),
            root.filePath(QStringLiteral("installed")), hooks);
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("could not activate")));
        QCOMPARE(renameCalls, 3);
        QCOMPARE(readAll(root.filePath(QStringLiteral("installed/1/song.toml"))),
            QByteArray("old\n"));
        QVERIFY(QFileInfo::exists(root.filePath(QStringLiteral("staged/1/song.toml"))));
        const QStringList backups = root.entryList(
            { QStringLiteral("installed.backup-*") }, QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY(backups.isEmpty());
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
