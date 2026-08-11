// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "CorpusArchive.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#include <zip.h>

#include <algorithm>
#include <memory>

namespace ope::corpus {
namespace {

constexpr zip_uint64_t MaxEntries = 10'000;
constexpr zip_uint64_t MaxFileBytes = 32 * 1024 * 1024;
constexpr zip_uint64_t MaxExpandedBytes = 256 * 1024 * 1024;

using ZipPtr = std::unique_ptr<zip_t, decltype(&zip_discard)>;
using ZipFilePtr = std::unique_ptr<zip_file_t, decltype(&zip_fclose)>;

QString zipError(zip_t *archive, const QString &context)
{
    return QStringLiteral("%1: %2")
        .arg(context, QString::fromUtf8(zip_strerror(archive)));
}

std::expected<QString, QString> safeEntryPath(const QString &raw, QString &archiveRoot)
{
    if (raw.isEmpty() || raw.startsWith(u'/') || raw.contains(u'\\'))
        return std::unexpected(QStringLiteral("unsafe archive path: %1").arg(raw));

    const QString clean = QDir::cleanPath(raw);
    if (clean == QLatin1String("..") || clean.startsWith(QLatin1String("../"))
        || clean.contains(QLatin1String("/../")) || clean == QLatin1String(".")) {
        return std::unexpected(QStringLiteral("unsafe archive path: %1").arg(raw));
    }

    const qsizetype slash = clean.indexOf(u'/');
    const QString root = slash < 0 ? clean : clean.first(slash);
    if (root.isEmpty() || root == QLatin1String(".") || root == QLatin1String(".."))
        return std::unexpected(QStringLiteral("invalid archive root: %1").arg(raw));
    if (archiveRoot.isEmpty())
        archiveRoot = root;
    else if (archiveRoot != root)
        return std::unexpected(QStringLiteral("archive has more than one top-level directory"));

    if (slash < 0)
        return QString();
    const QString relative = clean.sliced(slash + 1);
    if (relative.isEmpty() || relative == QLatin1String("."))
        return QString();
    if (relative.contains(u':'))
        return std::unexpected(QStringLiteral("unsafe archive path: %1").arg(raw));
    return relative;
}

bool copyTree(const QString &source, const QString &destination, QString &error)
{
    if (!QDir().mkpath(destination)) {
        error = QStringLiteral("could not create staging directory %1").arg(destination);
        return false;
    }
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    const QDir sourceDir(source);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = sourceDir.relativeFilePath(sourcePath);
        const QString destinationPath = QDir(destination).filePath(relative);
        if (info.isSymLink()) {
            error = QStringLiteral("refusing symbolic link in staged corpus: %1").arg(relative);
            return false;
        }
        if (info.isDir()) {
            if (!QDir().mkpath(destinationPath)) {
                error = QStringLiteral("could not create %1").arg(destinationPath);
                return false;
            }
            continue;
        }
        if (!info.isFile()) {
            error = QStringLiteral("unsupported staged entry: %1").arg(relative);
            return false;
        }
        if (!QDir().mkpath(QFileInfo(destinationPath).path())
            || !QFile::copy(sourcePath, destinationPath)) {
            error = QStringLiteral("could not copy %1 to installation staging").arg(relative);
            return false;
        }
    }
    return true;
}

} // namespace

std::expected<ExtractResult, QString> extractSnapshotZip(
    const QString &archivePath, const QString &destination)
{
    if (archivePath.isEmpty() || destination.isEmpty())
        return std::unexpected(QStringLiteral("archive and destination paths are required"));
    const QDir destinationDir(destination);
    if (destinationDir.exists()
        && !destinationDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
        return std::unexpected(QStringLiteral("extraction destination is not empty: %1")
                                   .arg(destination));
    }
    if (!QDir().mkpath(destination))
        return std::unexpected(QStringLiteral("could not create %1").arg(destination));

    int errorCode = 0;
    ZipPtr archive(zip_open(QFile::encodeName(archivePath).constData(), ZIP_RDONLY, &errorCode),
        &zip_discard);
    if (!archive) {
        zip_error_t error;
        zip_error_init_with_code(&error, errorCode);
        const QString message = QString::fromUtf8(zip_error_strerror(&error));
        zip_error_fini(&error);
        return std::unexpected(QStringLiteral("could not open downloaded ZIP: %1").arg(message));
    }

    const zip_int64_t entryCount = zip_get_num_entries(archive.get(), 0);
    if (entryCount <= 0 || static_cast<zip_uint64_t>(entryCount) > MaxEntries)
        return std::unexpected(QStringLiteral("archive has an unsafe entry count: %1")
                                   .arg(entryCount));

    ExtractResult result;
    zip_uint64_t expandedBytes = 0;
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(entryCount); ++index) {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(archive.get(), index, 0, &stat) != 0)
            return std::unexpected(
                zipError(archive.get(), QStringLiteral("could not inspect ZIP")));
        const QString rawName = QString::fromUtf8(stat.name ? stat.name : "");
        auto relativeResult = safeEntryPath(rawName, result.archiveRoot);
        if (!relativeResult)
            return std::unexpected(relativeResult.error());
        const QString relative = *relativeResult;
        if (relative.isEmpty())
            continue;

        const bool directory = rawName.endsWith(u'/');
        zip_uint8_t opsys = 0;
        zip_uint32_t attributes = 0;
        if (zip_file_get_external_attributes(
                archive.get(), index, 0, &opsys, &attributes)
            == 0
            && opsys == ZIP_OPSYS_UNIX) {
            const zip_uint32_t fileType = (attributes >> 16) & 0170000;
            if (fileType != 0 && fileType != 0040000 && fileType != 0100000)
                return std::unexpected(QStringLiteral("archive contains a link or special file: %1")
                                           .arg(rawName));
        }

        const QString outputPath = QDir(destination).filePath(relative);
        if (directory) {
            if (!QDir().mkpath(outputPath))
                return std::unexpected(QStringLiteral("could not create %1").arg(outputPath));
            continue;
        }
        if (!(stat.valid & ZIP_STAT_SIZE) || stat.size > MaxFileBytes
            || expandedBytes + stat.size > MaxExpandedBytes) {
            return std::unexpected(QStringLiteral("archive entry is too large: %1").arg(rawName));
        }
        if (!QDir().mkpath(QFileInfo(outputPath).path()))
            return std::unexpected(QStringLiteral("could not create directory for %1")
                                       .arg(outputPath));

        ZipFilePtr input(zip_fopen_index(archive.get(), index, ZIP_FL_UNCHANGED), &zip_fclose);
        if (!input)
            return std::unexpected(zipError(archive.get(), QStringLiteral("could not read %1")
                                                               .arg(rawName)));
        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly))
            return std::unexpected(QStringLiteral("could not create %1: %2")
                                       .arg(outputPath, output.errorString()));

        QByteArray buffer(64 * 1024, Qt::Uninitialized);
        zip_uint64_t written = 0;
        while (written < stat.size) {
            const zip_uint64_t wanted
                = std::min<zip_uint64_t>(buffer.size(), stat.size - written);
            const zip_int64_t got = zip_fread(input.get(), buffer.data(), wanted);
            if (got <= 0)
                return std::unexpected(QStringLiteral("truncated or unreadable ZIP entry: %1")
                                           .arg(rawName));
            if (output.write(buffer.constData(), got) != got)
                return std::unexpected(QStringLiteral("could not write %1: %2")
                                           .arg(outputPath, output.errorString()));
            written += static_cast<zip_uint64_t>(got);
        }
        if (!output.commit())
            return std::unexpected(QStringLiteral("could not finish %1: %2")
                                       .arg(outputPath, output.errorString()));
        ++result.files;
        expandedBytes += stat.size;
    }
    result.bytes = static_cast<qint64>(expandedBytes);
    if (result.files == 0 || result.archiveRoot.isEmpty())
        return std::unexpected(QStringLiteral("downloaded ZIP contains no files"));
    return result;
}

std::expected<InstallResult, QString> installValidatedSnapshot(
    const QString &stagingRoot, const QString &target)
{
    const QFileInfo sourceInfo(stagingRoot);
    const QString absoluteTarget = QFileInfo(target).absoluteFilePath();
    if (!sourceInfo.isDir() || target.trimmed().isEmpty() || QDir(absoluteTarget).isRoot())
        return std::unexpected(QStringLiteral("unsafe or missing corpus installation path"));
    if (QFileInfo::exists(absoluteTarget) && !QFileInfo(absoluteTarget).isDir())
        return std::unexpected(QStringLiteral("corpus target exists but is not a directory: %1")
                                   .arg(absoluteTarget));

    const QString parentPath = QFileInfo(absoluteTarget).path();
    if (!QDir().mkpath(parentPath))
        return std::unexpected(QStringLiteral("could not create %1").arg(parentPath));
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString copyPath = QDir(parentPath).filePath(QStringLiteral(".ope-corpus-stage-%1")
                                                          .arg(suffix));
    QString copyError;
    if (!copyTree(stagingRoot, copyPath, copyError)) {
        QDir(copyPath).removeRecursively();
        return std::unexpected(copyError);
    }

    InstallResult result { absoluteTarget, QString() };
    if (QFileInfo::exists(absoluteTarget)) {
        result.backup = QStringLiteral("%1.backup-%2")
                            .arg(absoluteTarget,
                                QDateTime::currentDateTimeUtc().toString(
                                    QStringLiteral("yyyyMMdd-HHmmss")));
        if (QFileInfo::exists(result.backup))
            result.backup.append(u'-' + suffix.first(8));
        if (!QDir().rename(absoluteTarget, result.backup)) {
            QDir(copyPath).removeRecursively();
            return std::unexpected(QStringLiteral("could not move the existing corpus to %1")
                                       .arg(result.backup));
        }
    }

    if (!QDir().rename(copyPath, absoluteTarget)) {
        QString rollback;
        if (!result.backup.isEmpty() && !QDir().rename(result.backup, absoluteTarget))
            rollback = QStringLiteral(" The previous corpus also could not be restored from %1.")
                           .arg(result.backup);
        QDir(copyPath).removeRecursively();
        return std::unexpected(QStringLiteral("could not activate the downloaded corpus.%1")
                                   .arg(rollback));
    }
    return result;
}

} // namespace ope::corpus
