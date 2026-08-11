// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "CorpusDownloadDialog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace ope::ui {
namespace {

constexpr qsizetype MaxHeadResponseBytes = 1024 * 1024;

QNetworkRequest githubRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("OpenPsalm-Editor/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    return request;
}

} // namespace

QNetworkRequest corpusHeadRequest()
{
    QNetworkRequest request = githubRequest(QUrl(QString::fromLatin1(CorpusHeadApiUrl)));
    request.setTransferTimeout(15'000);
    return request;
}

QString ResolvedCorpusHead::archiveUrl() const
{
    return QStringLiteral("https://api.github.com/repos/squinky86/OP-songs/zipball/%1")
        .arg(sha);
}

std::expected<ResolvedCorpusHead, QString> parseCorpusHeadResponse(
    const QByteArray &json, const QDateTime &checkedAt)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::unexpected(QStringLiteral("GitHub returned invalid commit metadata: %1")
                                   .arg(error.errorString()));
    const QJsonObject object = document.object();
    ResolvedCorpusHead result;
    result.sha = object.value(QStringLiteral("sha")).toString().toLower();
    static const QRegularExpression validSha(QStringLiteral("^[0-9a-f]{40,64}$"));
    if (!validSha.match(result.sha).hasMatch())
        return std::unexpected(QStringLiteral("GitHub commit metadata has no valid SHA."));
    const QJsonObject commit = object.value(QStringLiteral("commit")).toObject();
    result.committedAt = QDateTime::fromString(
        commit.value(QStringLiteral("committer"))
            .toObject()
            .value(QStringLiteral("date"))
            .toString(),
        Qt::ISODate);
    if (!result.committedAt.isValid())
        return std::unexpected(QStringLiteral("GitHub commit metadata has no valid date."));
    result.checkedAt = checkedAt.isValid() ? checkedAt.toUTC() : QDateTime::currentDateTimeUtc();
    return result;
}

CorpusDownloadDialog::CorpusDownloadDialog(QString target, QWidget *parent)
    : CorpusDownloadDialog(std::move(target), {}, parent)
{
}

CorpusDownloadDialog::CorpusDownloadDialog(
    QString target, CorpusDownloadOptions options, QWidget *parent)
    : QDialog(parent)
    , m_target(std::move(target))
    , m_network(options.network ? options.network : &m_ownedNetwork)
    , m_maxDownloadBytes(options.maxDownloadBytes)
    , m_resolvedHeadOverride(std::move(options.resolvedHead))
    , m_temporary(QDir::temp().filePath(QStringLiteral("openpsalm-corpus-XXXXXX")))
{
    setWindowTitle(tr("Download Latest OP-songs"));
    setModal(true);
    setMinimumWidth(540);

    auto *layout = new QVBoxLayout(this);
    m_stage = new QLabel(tr("Preparing download…"), this);
    m_stage->setWordWrap(true);
    layout->addWidget(m_stage);
    m_detail = new QLabel(tr("Nothing in the destination is changed until the download "
                              "passes every integrity check."), this);
    m_detail->setWordWrap(true);
    m_detail->setStyleSheet(QStringLiteral("color: #57606a;"));
    layout->addWidget(m_detail);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    layout->addWidget(m_progress);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_cancel = buttons->button(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &CorpusDownloadDialog::reject);
    QTimer::singleShot(0, this, &CorpusDownloadDialog::start);
}

void CorpusDownloadDialog::setStage(const QString &message)
{
    m_stage->setText(message);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void CorpusDownloadDialog::reject()
{
    // Installation happens before the completion page is shown. Treat any way
    // of closing that page as success so MainWindow switches to the corpus that
    // is already active on disk.
    if (!m_installResult.target.isEmpty()) {
        QDialog::accept();
        return;
    }
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();
    if (m_validating) {
        m_stage->setText(tr("Cancelling validation…"));
        m_cancel->setEnabled(false);
        return;
    }
    QDialog::reject();
}

void CorpusDownloadDialog::start()
{
    if (m_maxDownloadBytes <= 0) {
        fail(tr("The configured download safety limit is invalid."));
        return;
    }
    if (!m_temporary.isValid()) {
        fail(tr("Could not create a temporary staging directory."));
        return;
    }

    if (m_resolvedHeadOverride) {
        m_head = *m_resolvedHeadOverride;
        startArchiveDownload();
    } else {
        startHeadResolution();
    }
}

void CorpusDownloadDialog::startHeadResolution()
{
    setStage(tr("Resolving the latest OP-songs commit…"));
    m_reply = m_network->get(corpusHeadRequest());
    connect(m_reply, &QIODevice::readyRead, this, [this] {
        m_headResponse.append(m_reply->readAll());
        if (m_headResponse.size() > MaxHeadResponseBytes) {
            m_failure = tr("GitHub commit metadata exceeded the safety limit.");
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &CorpusDownloadDialog::headFinished);
}

void CorpusDownloadDialog::headFinished()
{
    m_headResponse.append(m_reply->readAll());
    if (m_headResponse.size() > MaxHeadResponseBytes && m_failure.isEmpty())
        m_failure = tr("GitHub commit metadata exceeded the safety limit.");
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkMessage = m_reply->errorString();
    m_reply->deleteLater();
    m_reply = nullptr;
    if (m_cancelled) {
        QDialog::reject();
        return;
    }
    if (!m_failure.isEmpty()) {
        fail(m_failure);
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        fail(tr("Could not resolve OP-songs HEAD: %1").arg(networkMessage));
        return;
    }
    const auto parsed = parseCorpusHeadResponse(m_headResponse, QDateTime::currentDateTimeUtc());
    m_headResponse.clear();
    if (!parsed) {
        fail(parsed.error());
        return;
    }
    m_head = *parsed;
    startArchiveDownload();
}

void CorpusDownloadDialog::startArchiveDownload()
{
    setStage(tr("Downloading OP-songs commit %1…").arg(corpus::abbreviatedSha(m_head.sha)));
    m_failure.clear();
    m_reply = m_network->get(githubRequest(QUrl(m_head.archiveUrl())));
    connect(m_reply, &QNetworkReply::downloadProgress, this,
        [this](qint64 received, qint64 total) {
            if (total > 0 && total <= std::numeric_limits<int>::max()) {
                m_progress->setRange(0, static_cast<int>(total));
                m_progress->setValue(static_cast<int>(std::min(received, total)));
            } else {
                m_progress->setRange(0, 0);
            }
        });
    connect(m_reply, &QIODevice::readyRead, this, [this] {
        m_download.append(m_reply->readAll());
        if (m_download.size() > m_maxDownloadBytes) {
            m_failure = tr("The download exceeded the %1 byte safety limit.")
                            .arg(m_maxDownloadBytes);
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &CorpusDownloadDialog::downloadFinished);
}

void CorpusDownloadDialog::downloadFinished()
{
    m_download.append(m_reply->readAll());
    if (m_download.size() > m_maxDownloadBytes && m_failure.isEmpty()) {
        m_failure = tr("The download exceeded the %1 byte safety limit.")
                        .arg(m_maxDownloadBytes);
    }
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkMessage = m_reply->errorString();
    m_finalUrl = m_reply->url().toString(QUrl::FullyEncoded);
    m_etag = QString::fromLatin1(m_reply->rawHeader("ETag"));
    m_reply->deleteLater();
    m_reply = nullptr;
    if (m_cancelled) {
        QDialog::reject();
        return;
    }
    if (!m_failure.isEmpty()) {
        fail(m_failure);
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        fail(tr("The OP-songs download failed: %1").arg(networkMessage));
        return;
    }
    if (m_download.isEmpty()) {
        fail(tr("GitHub returned an empty archive."));
        return;
    }
    const QString archiveSha256
        = QString::fromLatin1(QCryptographicHash::hash(m_download, QCryptographicHash::Sha256)
                                  .toHex());

    m_progress->setRange(0, 0);
    setStage(tr("Checking and extracting the downloaded archive…"));
    const QString archivePath = m_temporary.filePath(QStringLiteral("OP-songs-main.zip"));
    QSaveFile archive(archivePath);
    if (!archive.open(QIODevice::WriteOnly) || archive.write(m_download) != m_download.size()
        || !archive.commit()) {
        fail(tr("Could not stage the downloaded archive: %1").arg(archive.errorString()));
        return;
    }
    m_download.clear();

    const QString unpacked = m_temporary.filePath(QStringLiteral("unpacked"));
    const auto extracted = corpus::extractSnapshotZip(archivePath, unpacked);
    if (!extracted) {
        fail(tr("The downloaded archive was rejected: %1").arg(extracted.error()));
        return;
    }

    corpus::SnapshotInfo snapshot;
    snapshot.commitSha = m_head.sha;
    snapshot.commitDate = m_head.committedAt;
    snapshot.currentAsOf = m_head.checkedAt;
    snapshot.downloadedAt = QDateTime::currentDateTimeUtc();
    snapshot.requestedUrl = m_head.archiveUrl();
    snapshot.downloadUrl = m_finalUrl;
    snapshot.archiveSha256 = archiveSha256;
    snapshot.httpEtag = m_etag;
    snapshot.archiveRoot = extracted->archiveRoot;
    snapshot.editorVersion = QCoreApplication::applicationVersion();
    if (auto written = corpus::writeSnapshot(unpacked, snapshot); !written) {
        fail(tr("Could not record download provenance: %1").arg(written.error()));
        return;
    }

    setStage(tr("Validating %n extracted file(s)…", nullptr, extracted->files));
    cli::Options options;
    options.root = unpacked;
    options.warnings = true;
    options.info = true;
    options.progress = [this](int completed, int total, const QString &path) {
        m_progress->setRange(0, total);
        m_progress->setValue(completed);
        m_detail->setText(tr("Checking %1 of %2:\n%3").arg(completed).arg(total).arg(path));
    };
    options.cancelled = [this] {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        return m_cancelled;
    };
    m_validating = true;
    m_checkSummary = cli::check(options);
    m_validating = false;
    if (m_cancelled) {
        QDialog::reject();
        return;
    }
    if (!m_checkSummary.passed()) {
        fail(tr("The downloaded corpus did not pass integrity checking. The installed corpus "
                "was not changed.\n\n%1")
                 .arg(m_checkSummary.description()));
        return;
    }

    setStage(tr("Installing the validated corpus…"));
    const auto installed = corpus::installValidatedSnapshot(unpacked, m_target);
    if (!installed) {
        fail(tr("The validated corpus could not be installed: %1").arg(installed.error()));
        return;
    }
    m_installResult = *installed;
    m_progress->setRange(0, 1);
    m_progress->setValue(1);
    m_stage->setText(tr("The latest OP-songs corpus is ready."));
    QString detail = tr("Commit: %1\nCommitted: %2\nCurrent as of: %3\n\n%4")
                         .arg(m_head.sha,
                             QLocale::system().toString(
                                 m_head.committedAt.toLocalTime(), QLocale::ShortFormat),
                             QLocale::system().toString(
                                 m_head.checkedAt.toLocalTime(), QLocale::ShortFormat),
                             m_checkSummary.description());
    if (!m_installResult.backup.isEmpty())
        detail += tr("\n\nThe previous directory was retained at:\n%1")
                      .arg(m_installResult.backup);
    m_detail->setText(detail);
    m_cancel->setText(tr("Continue"));
    disconnect(m_cancel, nullptr, this, nullptr);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::accept);
}

void CorpusDownloadDialog::fail(const QString &message)
{
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_stage->setText(tr("Corpus update failed"));
    m_detail->setText(message);
    m_detail->setStyleSheet(QStringLiteral("color: #d1242f;"));
    m_cancel->setText(tr("Close"));
}

} // namespace ope::ui
