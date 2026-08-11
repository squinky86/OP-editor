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
#include <QJsonDocument>
#include <QJsonObject>
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
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        if (m_reply)
            m_reply->abort();
        reject();
    });
    QTimer::singleShot(0, this, &CorpusDownloadDialog::start);
}

void CorpusDownloadDialog::setStage(const QString &message)
{
    m_stage->setText(message);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
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

    setStage(tr("Downloading the head of OP-songs/main…"));
    QNetworkRequest request(QUrl(QString::fromLatin1(corpus::HeadArchiveUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("OpenPsalm-Editor/%1").arg(QCoreApplication::applicationVersion()));
    m_reply = m_network->get(request);
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
    const QNetworkReply::NetworkError networkError = m_reply->error();
    const QString networkMessage = m_reply->errorString();
    m_finalUrl = m_reply->url().toString(QUrl::FullyEncoded);
    m_etag = QString::fromLatin1(m_reply->rawHeader("ETag"));
    m_reply->deleteLater();
    m_reply = nullptr;
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

    QJsonObject provenance;
    provenance.insert(QStringLiteral("schema"), 1);
    provenance.insert(QStringLiteral("repository"), QStringLiteral("squinky86/OP-songs"));
    provenance.insert(QStringLiteral("branch"), QStringLiteral("main"));
    provenance.insert(QStringLiteral("requested_url"),
        QString::fromLatin1(corpus::HeadArchiveUrl));
    provenance.insert(QStringLiteral("download_url"), m_finalUrl);
    provenance.insert(QStringLiteral("downloaded_at"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    provenance.insert(QStringLiteral("archive_sha256"), archiveSha256);
    provenance.insert(QStringLiteral("http_etag"), m_etag);
    provenance.insert(QStringLiteral("archive_root"), extracted->archiveRoot);
    provenance.insert(QStringLiteral("editor_version"),
        QCoreApplication::applicationVersion());
    QSaveFile provenanceFile(
        QDir(unpacked).filePath(QStringLiteral(".openpsalm-snapshot.json")));
    const QByteArray provenanceBytes = QJsonDocument(provenance).toJson();
    if (!provenanceFile.open(QIODevice::WriteOnly)
        || provenanceFile.write(provenanceBytes) != provenanceBytes.size()
        || !provenanceFile.commit()) {
        fail(tr("Could not record download provenance: %1")
                 .arg(provenanceFile.errorString()));
        return;
    }

    setStage(tr("Validating %n extracted file(s)…", nullptr, extracted->files));
    cli::Options options;
    options.root = unpacked;
    options.warnings = true;
    options.info = true;
    m_checkSummary = cli::check(options);
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
    QString detail = m_checkSummary.description();
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
