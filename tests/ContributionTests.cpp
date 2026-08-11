// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Fixtures.h"

#include "cli/Contribution.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace ope;
using namespace ope::contrib;
using namespace ope::fixtures;

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

Request correction(const QString &output)
{
    Request request;
    request.outputParent = output;
    request.workId = 42;
    request.title = QStringLiteral("Face to Face");
    request.language = QStringLiteral("en");
    request.fileName = QStringLiteral("song.toml");
    request.editorVersion = QStringLiteral("test");
    request.baselineToml = baseSong();
    request.proposedToml = request.baselineToml;
    request.proposedToml.replace("Face to Face", "Face to Face — corrected");
    return request;
}

} // namespace

class ContributionTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void correctionCreatesCheckedReviewBundleAndZip()
    {
        QTemporaryDir output;
        QVERIFY(output.isValid());
        const auto result = prepare(correction(output.path()));
        const QString error = result ? QString() : result.error();
        QVERIFY2(result, qPrintable(error));
        QVERIFY(result->checks.passed());
        QVERIFY(!result->newFile);
        QVERIFY(QFileInfo::exists(result->proposedFile));
        QVERIFY(QFileInfo::exists(result->patchFile));
        QVERIFY(QFileInfo::exists(result->reportFile));
        QVERIFY(QFileInfo::exists(result->hashesFile));
        QVERIFY(QFileInfo(result->archive).size() > 0);
        QCOMPARE(result->archiveSha256.size(), 64);

        const QByteArray patch = readAll(result->patchFile);
        QVERIFY(patch.contains("--- a/42/song.toml"));
        QVERIFY(patch.contains("-title = \"Face to Face\""));
        QVERIFY(patch.contains("+title = \"Face to Face — corrected\""));
        QVERIFY(readAll(result->hashesFile).contains("files/42/song.toml"));
    }

    void syntaxAndValidationErrorsBlockBundleCreation()
    {
        QTemporaryDir output;
        QVERIFY(output.isValid());
        Request request = correction(output.path());
        request.proposedToml = QByteArray("title = [\n");
        auto result = prepare(request);
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("preflight failed")));

        request.proposedToml = QByteArray("title = \"No music\"\n");
        result = prepare(request);
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("validation error")));
        QCOMPARE(QDir(output.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }

    void unchangedExistingFileIsNotAContribution()
    {
        QTemporaryDir output;
        Request request = correction(output.path());
        request.proposedToml = request.baselineToml;
        const auto result = prepare(request);
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("no contribution changes")));
    }

    void unsafeFilenameCannotEscapeTheValidationOrBundleDirectory()
    {
        QTemporaryDir output;
        Request request = correction(output.path());
        request.language = QStringLiteral("../../escape");
        request.fileName = QStringLiteral("song_../../escape.toml");
        const auto result = prepare(request);
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("Unsafe")));
        QVERIFY(!QFileInfo::exists(QDir(output.path()).filePath(QStringLiteral("escape.toml"))));
    }

    void newSongSubmissionDoesNotProposeAnUpstreamId()
    {
        QTemporaryDir output;
        Request request = correction(output.path());
        request.baselineToml.clear();
        request.workId = 0;
        const auto result = prepare(request);
        const QString error = result ? QString() : result.error();
        QVERIFY2(result, qPrintable(error));
        QVERIFY(result->newFile);
        QVERIFY(result->newSong);
        QCOMPARE(QFileInfo(result->proposedFile).fileName(), QStringLiteral("song.toml"));
        QCOMPARE(QFileInfo(result->proposedFile).absolutePath(), result->directory);
        QCOMPARE(readAll(result->proposedFile), request.proposedToml);

        const QByteArray patch = readAll(result->patchFile);
        QVERIFY(patch.startsWith("--- /dev/null\n+++ b/song.toml\n"));
        QVERIFY(!patch.contains("/0/"));

        const QByteArray report = readAll(result->reportFile);
        QVERIFY(report.contains("Song: New song"));
        QVERIFY(report.contains("maintainer assigns its directory ID"));
        QVERIFY(!report.contains("#0"));
        QVERIFY(!result->directory.contains(QStringLiteral("song-0")));
        QVERIFY(readAll(result->hashesFile).contains("  song.toml\n"));
    }

    void translationIsCheckedAgainstItsBase()
    {
        QTemporaryDir output;
        Request request;
        request.outputParent = output.path();
        request.workId = 42;
        request.title = QStringLiteral("Cara a cara");
        request.language = QStringLiteral("es");
        request.fileName = QStringLiteral("song_es.toml");
        request.editorVersion = QStringLiteral("test");
        request.baseToml = baseSong();
        request.proposedToml = QByteArray("title = \"Cara a cara\"\n");
        const auto result = prepare(request);
        const QString error = result ? QString() : result.error();
        QVERIFY2(result, qPrintable(error));
        QVERIFY(result->checks.passed());
        QVERIFY(result->newFile);
        QVERIFY(!result->newSong);
        QVERIFY(result->proposedFile.endsWith(QStringLiteral("files/42/song_es.toml")));
    }

    void newTranslationStillRequiresItsExistingSongId()
    {
        QTemporaryDir output;
        Request request;
        request.outputParent = output.path();
        request.title = QStringLiteral("Cara a cara");
        request.language = QStringLiteral("es");
        request.fileName = QStringLiteral("song_es.toml");
        request.baseToml = baseSong();
        request.proposedToml = QByteArray("title = \"Cara a cara\"\n");
        const auto result = prepare(request);
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("song identity")));
    }
};

QTEST_APPLESS_MAIN(ContributionTests)
#include "ContributionTests.moc"
