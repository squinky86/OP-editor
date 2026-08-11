// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "CorpusSnapshot.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace ope::corpus {
namespace {

QDateTime readDate(const QJsonObject &object, const QString &key)
{
    return QDateTime::fromString(object.value(key).toString(), Qt::ISODate);
}

void insertDate(QJsonObject &object, const QString &key, const QDateTime &value)
{
    if (value.isValid())
        object.insert(key, value.toUTC().toString(Qt::ISODate));
}

bool validSha(const QString &sha)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9a-fA-F]{40,64}$"));
    return expression.match(sha).hasMatch();
}

} // namespace

bool SnapshotInfo::hasResolvedCommit() const noexcept
{
    return validSha(commitSha) && commitDate.isValid();
}

QString snapshotPath(const QString &corpusRoot)
{
    return QDir(corpusRoot).filePath(QString::fromLatin1(SnapshotFileName));
}

std::expected<SnapshotInfo, QString> readSnapshot(const QString &corpusRoot)
{
    QFile file(snapshotPath(corpusRoot));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(QStringLiteral("Could not read snapshot metadata: %1")
                                   .arg(file.errorString()));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(QStringLiteral("Invalid snapshot metadata: %1")
                                   .arg(parseError.errorString()));
    }
    const QJsonObject object = document.object();
    SnapshotInfo result;
    result.schema = object.value(QStringLiteral("schema")).toInt(1);
    result.repository = object.value(QStringLiteral("repository")).toString();
    result.branch = object.value(QStringLiteral("branch")).toString();
    result.commitSha = object.value(QStringLiteral("commit_sha")).toString().toLower();
    result.commitDate = readDate(object, QStringLiteral("commit_date"));
    result.currentAsOf = readDate(object, QStringLiteral("current_as_of"));
    result.downloadedAt = readDate(object, QStringLiteral("downloaded_at"));
    result.requestedUrl = object.value(QStringLiteral("requested_url")).toString();
    result.downloadUrl = object.value(QStringLiteral("download_url")).toString();
    result.archiveSha256 = object.value(QStringLiteral("archive_sha256")).toString();
    result.httpEtag = object.value(QStringLiteral("http_etag")).toString();
    result.archiveRoot = object.value(QStringLiteral("archive_root")).toString();
    result.editorVersion = object.value(QStringLiteral("editor_version")).toString();
    return result;
}

std::expected<void, QString> writeSnapshot(
    const QString &corpusRoot, const SnapshotInfo &snapshot)
{
    if (!QDir().mkpath(corpusRoot))
        return std::unexpected(QStringLiteral("Could not create %1").arg(corpusRoot));
    QJsonObject object;
    object.insert(QStringLiteral("schema"), snapshot.schema);
    object.insert(QStringLiteral("repository"), snapshot.repository);
    object.insert(QStringLiteral("branch"), snapshot.branch);
    object.insert(QStringLiteral("commit_sha"), snapshot.commitSha.toLower());
    insertDate(object, QStringLiteral("commit_date"), snapshot.commitDate);
    insertDate(object, QStringLiteral("current_as_of"), snapshot.currentAsOf);
    insertDate(object, QStringLiteral("downloaded_at"), snapshot.downloadedAt);
    object.insert(QStringLiteral("requested_url"), snapshot.requestedUrl);
    object.insert(QStringLiteral("download_url"), snapshot.downloadUrl);
    object.insert(QStringLiteral("archive_sha256"), snapshot.archiveSha256);
    object.insert(QStringLiteral("http_etag"), snapshot.httpEtag);
    object.insert(QStringLiteral("archive_root"), snapshot.archiveRoot);
    object.insert(QStringLiteral("editor_version"), snapshot.editorVersion);

    QSaveFile file(snapshotPath(corpusRoot));
    const QByteArray bytes = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit()) {
        return std::unexpected(QStringLiteral("Could not write snapshot metadata: %1")
                                   .arg(file.errorString()));
    }
    return {};
}

std::expected<void, QString> markSnapshotCurrent(const QString &corpusRoot,
    const QString &headSha, const QDateTime &headCommitDate, const QDateTime &checkedAt)
{
    auto snapshot = readSnapshot(corpusRoot);
    if (!snapshot)
        return std::unexpected(snapshot.error());
    if (!validSha(headSha) || snapshot->commitSha.compare(headSha, Qt::CaseInsensitive) != 0)
        return std::unexpected(QStringLiteral("Installed snapshot does not match resolved HEAD."));
    snapshot->commitDate = headCommitDate;
    snapshot->currentAsOf = checkedAt;
    return writeSnapshot(corpusRoot, *snapshot);
}

QStringList backupDirectories(const QString &target)
{
    const QFileInfo targetInfo(QFileInfo(target).absoluteFilePath());
    const QDir parent(targetInfo.path());
    const QString prefix = targetInfo.fileName() + QStringLiteral(".backup-");
    QStringList result;
    for (const QFileInfo &entry : parent.entryInfoList(
             { prefix + u'*' }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        if (!entry.isSymLink())
            result.append(entry.absoluteFilePath());
    }
    return result;
}

QString abbreviatedSha(const QString &sha)
{
    return sha.size() > 10 ? sha.first(10) : sha;
}

} // namespace ope::corpus
