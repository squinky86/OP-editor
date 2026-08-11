// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Contribution.h"

#include "core/Library.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUuid>

#include <zip.h>

#include <algorithm>

namespace ope::contrib {
namespace {

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

std::expected<void, QString> writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).path()))
        return std::unexpected(QStringLiteral("Could not create %1").arg(QFileInfo(path).path()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return std::unexpected(QStringLiteral("Could not create %1: %2")
                                   .arg(path, file.errorString()));
    if (file.write(bytes) != bytes.size() || !file.commit())
        return std::unexpected(QStringLiteral("Could not write %1: %2")
                                   .arg(path, file.errorString()));
    return {};
}

QList<QByteArray> lines(const QByteArray &bytes)
{
    QList<QByteArray> result = bytes.split('\n');
    if (bytes.endsWith('\n') && !result.isEmpty())
        result.removeLast();
    return result;
}

QByteArray unifiedPatch(const Request &request, bool newFile, bool newSong)
{
    const QByteArray relative = newSong
        ? request.fileName.toUtf8()
        : QStringLiteral("%1/%2").arg(request.workId).arg(request.fileName).toUtf8();
    const QList<QByteArray> before = lines(request.baselineToml);
    const QList<QByteArray> after = lines(request.proposedToml);

    QByteArray patch;
    patch += newFile ? QByteArray("--- /dev/null\n") : QByteArray("--- a/") + relative + '\n';
    patch += QByteArray("+++ b/") + relative + '\n';
    if (newFile) {
        patch += QByteArray("@@ -0,0 +1,") + QByteArray::number(after.size()) + " @@\n";
        for (const QByteArray &line : after)
            patch += '+' + line + '\n';
        return patch;
    }

    qsizetype prefix = 0;
    while (prefix < before.size() && prefix < after.size()
        && before.at(prefix) == after.at(prefix))
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
        && before.at(before.size() - suffix - 1) == after.at(after.size() - suffix - 1))
        ++suffix;

    constexpr qsizetype Context = 3;
    const qsizetype start = std::max<qsizetype>(0, prefix - Context);
    const qsizetype trailing = std::min(Context, suffix);
    const qsizetype beforeEnd = before.size() - suffix + trailing;
    const qsizetype afterEnd = after.size() - suffix + trailing;
    patch += QByteArray("@@ -") + QByteArray::number(start + 1) + ','
        + QByteArray::number(beforeEnd - start) + " +" + QByteArray::number(start + 1) + ','
        + QByteArray::number(afterEnd - start) + " @@\n";
    for (qsizetype index = start; index < prefix; ++index)
        patch += ' ' + before.at(index) + '\n';
    for (qsizetype index = prefix; index < before.size() - suffix; ++index)
        patch += '-' + before.at(index) + '\n';
    for (qsizetype index = prefix; index < after.size() - suffix; ++index)
        patch += '+' + after.at(index) + '\n';
    for (qsizetype index = 0; index < trailing; ++index)
        patch += ' ' + before.at(before.size() - suffix + index) + '\n';
    return patch;
}

std::expected<void, QString> createZip(const QString &directory, const QString &archivePath)
{
    int errorCode = 0;
    zip_t *archive
        = zip_open(QFile::encodeName(archivePath).constData(), ZIP_CREATE | ZIP_EXCL, &errorCode);
    if (!archive) {
        zip_error_t error;
        zip_error_init_with_code(&error, errorCode);
        const QString message = QString::fromUtf8(zip_error_strerror(&error));
        zip_error_fini(&error);
        return std::unexpected(QStringLiteral("Could not create contribution ZIP: %1")
                                   .arg(message));
    }

    const QDir root(directory);
    QDirIterator it(directory, QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QByteArray pathBytes = QFile::encodeName(path);
        zip_source_t *source = zip_source_file(archive, pathBytes.constData(), 0, 0);
        const QByteArray relative = root.relativeFilePath(path).replace(u'\\', u'/').toUtf8();
        if (!source
            || zip_file_add(archive, relative.constData(), source, ZIP_FL_ENC_UTF_8) < 0) {
            if (source)
                zip_source_free(source);
            const QString message = QString::fromUtf8(zip_strerror(archive));
            zip_discard(archive);
            return std::unexpected(QStringLiteral("Could not add %1 to contribution ZIP: %2")
                                       .arg(path, message));
        }
    }
    if (zip_close(archive) != 0) {
        const QString message = QString::fromUtf8(zip_strerror(archive));
        zip_discard(archive);
        return std::unexpected(QStringLiteral("Could not finish contribution ZIP: %1")
                                   .arg(message));
    }
    return {};
}

QString uniqueBundleName(const Request &request, bool newSong)
{
    QString language = request.language;
    language.replace(u'-', u'_');
    if (newSong) {
        return QStringLiteral("OpenPsalm-new-song-%1")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    }
    return QStringLiteral("OpenPsalm-song-%1-%2-%3")
        .arg(request.workId)
        .arg(language,
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

} // namespace

std::expected<Bundle, QString> prepare(const Request &request)
{
    const bool newSong = request.baselineToml.isEmpty()
        && request.fileName == QStringLiteral("song.toml");
    if (request.outputParent.trimmed().isEmpty() || request.language.isEmpty()
        || request.fileName.isEmpty() || (!newSong && request.workId <= 0)) {
        return std::unexpected(
            QStringLiteral("Contribution destination and song identity are required."));
    }
    static const QRegularExpression safeLanguage(
        QStringLiteral("^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$"));
    if (!safeLanguage.match(request.language).hasMatch()
        || request.fileName != QFileInfo(request.fileName).fileName()
        || request.fileName.contains(u'/') || request.fileName.contains(u'\\')) {
        return std::unexpected(QStringLiteral("Unsafe contribution language or filename."));
    }
    const QString overlayLanguage = i18n::codeFromFilename(request.fileName);
    const bool overlay = !overlayLanguage.isEmpty();
    const QString expectedName = overlay
        ? QStringLiteral("song_%1.toml").arg(request.language)
        : QStringLiteral("song.toml");
    if (request.fileName != expectedName)
        return std::unexpected(
            QStringLiteral("The proposed filename must be %1.").arg(expectedName));
    if (overlay && overlayLanguage != request.language)
        return std::unexpected(QStringLiteral("The filename and language do not match."));
    if (overlay && request.baseToml.isEmpty())
        return std::unexpected(
            QStringLiteral("A translation must be checked with its base song.toml."));
    if (request.proposedToml.isEmpty())
        return std::unexpected(QStringLiteral("The proposed TOML is empty."));

    const bool newFile = request.baselineToml.isEmpty();
    if (!newFile && request.baselineToml == request.proposedToml)
        return std::unexpected(QStringLiteral("The current file has no contribution changes."));

    QTemporaryDir validation(QDir::temp().filePath(QStringLiteral("ope-contribution-XXXXXX")));
    if (!validation.isValid())
        return std::unexpected(QStringLiteral("Could not create a validation directory."));
    // Library validation needs a numeric directory. For a new song this is an
    // isolated implementation detail, not a proposed upstream corpus ID.
    const int validationId = newSong ? 1 : request.workId;
    const QString songDirectory
        = QDir(validation.path()).filePath(QString::number(validationId));
    if (overlay) {
        if (auto written = writeFile(QDir(songDirectory).filePath(QStringLiteral("song.toml")),
                request.baseToml);
            !written) {
            return std::unexpected(written.error());
        }
    }
    const QString validationFile = QDir(songDirectory).filePath(request.fileName);
    if (auto written = writeFile(validationFile, request.proposedToml); !written)
        return std::unexpected(written.error());

    cli::Options options;
    options.root = validationFile;
    options.info = true;
    const cli::CheckSummary checks = cli::check(options);
    if (!checks.passed()) {
        return std::unexpected(QStringLiteral("Contribution preflight failed. %1")
                                   .arg(checks.description()));
    }

    if (!QDir().mkpath(request.outputParent))
        return std::unexpected(QStringLiteral("Could not create %1.").arg(request.outputParent));
    QString name = uniqueBundleName(request, newSong);
    QString directory = QDir(request.outputParent).filePath(name);
    if (QFileInfo::exists(directory) || QFileInfo::exists(directory + QStringLiteral(".zip"))) {
        name += u'-' + QUuid::createUuid().toString(QUuid::WithoutBraces).first(8);
        directory = QDir(request.outputParent).filePath(name);
    }

    Bundle bundle;
    bundle.directory = directory;
    bundle.archive = directory + QStringLiteral(".zip");
    bundle.newFile = newFile;
    bundle.newSong = newSong;
    bundle.checks = checks;
    bundle.proposedFile = newSong
        ? QDir(directory).filePath(QStringLiteral("song.toml"))
        : QDir(directory).filePath(
              QStringLiteral("files/%1/%2").arg(request.workId).arg(request.fileName));
    bundle.patchFile = QDir(directory).filePath(QStringLiteral("changes.patch"));
    bundle.reportFile = QDir(directory).filePath(QStringLiteral("PREFLIGHT.md"));
    bundle.hashesFile = QDir(directory).filePath(QStringLiteral("SHA256SUMS.txt"));
    auto cleanup = qScopeGuard([&bundle] {
        if (!bundle.directory.isEmpty())
            QDir(bundle.directory).removeRecursively();
        if (!bundle.archive.isEmpty())
            QFile::remove(bundle.archive);
    });

    if (auto written = writeFile(bundle.proposedFile, request.proposedToml); !written)
        return std::unexpected(written.error());
    if (!request.copyrightFile.isEmpty()) {
        const QString copyrightPath = newSong
            ? QDir(directory).filePath(QStringLiteral("copyright.txt"))
            : QDir(directory).filePath(
                  QStringLiteral("files/%1/copyright.txt").arg(request.workId));
        if (auto written = writeFile(copyrightPath, request.copyrightFile); !written)
            return std::unexpected(written.error());
    }
    if (auto written = writeFile(bundle.patchFile, unifiedPatch(request, newFile, newSong));
        !written) {
        return std::unexpected(written.error());
    }

    const QString identity = newSong
        ? QStringLiteral("New song — %1").arg(request.title)
        : QStringLiteral("#%1 — %2").arg(request.workId).arg(request.title);
    const QString fileIdentity = newSong
        ? request.fileName
        : QStringLiteral("%1/%2").arg(request.workId).arg(request.fileName);
    const QString submission = newSong
        ? QStringLiteral(
              "Attach `song.toml` directly to the OP-songs issue, or paste its contents "
              "as a TOML code block. The corpus maintainer assigns its directory ID. The "
              "ZIP is an optional local review artifact.")
        : QStringLiteral(
              "Review `changes.patch`, cite the source for the change, and attach this ZIP "
              "to the OP-songs issue.");
    const QString report = QStringLiteral(
        "# OpenPsalm contribution preflight\n\n"
        "- Song: %1\n"
        "- Language: `%2`\n"
        "- File: `%3`\n"
        "- Change: %4\n"
        "- Generated: %5\n"
        "- OpenPsalm Editor: %6\n"
        "- Proposed TOML SHA-256: `%7`\n\n"
        "## Automated checks\n\n"
        "%8\n\n"
        "Errors block bundle creation. Warnings are included for human review and do not "
        "prove that musical content, attribution, or copyright status is correct. Review "
        "the source for the change. %9\n")
                               .arg(identity, request.language, fileIdentity,
                                   newFile ? QStringLiteral("new file")
                                           : QStringLiteral("update"),
                                   QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
                                   request.editorVersion, sha256(request.proposedToml),
                                   checks.description(), submission);
    if (auto written = writeFile(bundle.reportFile, report.toUtf8()); !written)
        return std::unexpected(written.error());

    QStringList hashes;
    QDirIterator hashFiles(directory, QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    const QDir bundleRoot(directory);
    while (hashFiles.hasNext()) {
        const QString path = hashFiles.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(QStringLiteral("Could not hash %1: %2")
                                       .arg(path, file.errorString()));
        hashes.append(QStringLiteral("%1  %2")
                          .arg(sha256(file.readAll()), bundleRoot.relativeFilePath(path)));
    }
    hashes.sort();
    if (auto written = writeFile(bundle.hashesFile, hashes.join(u'\n').toUtf8() + '\n'); !written)
        return std::unexpected(written.error());

    if (auto zipped = createZip(directory, bundle.archive); !zipped)
        return std::unexpected(zipped.error());
    QFile archive(bundle.archive);
    if (!archive.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("Could not hash contribution ZIP: %1")
                                   .arg(archive.errorString()));
    bundle.archiveSha256 = sha256(archive.readAll());
    cleanup.dismiss();
    return bundle;
}

} // namespace ope::contrib
