// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include "cli/Cli.h"
#include "core/CorpusArchive.h"
#include "core/CorpusSnapshot.h"

#include <QDateTime>
#include <QDialog>
#include <QNetworkAccessManager>
#include <QTemporaryDir>

#include <expected>
#include <optional>

QT_BEGIN_NAMESPACE
class QLabel;
class QNetworkReply;
class QNetworkRequest;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

namespace ope::ui {

inline constexpr auto CorpusHeadApiUrl
    = "https://api.github.com/repos/squinky86/OP-songs/commits/main";

struct ResolvedCorpusHead {
    QString sha;
    QDateTime committedAt;
    QDateTime checkedAt;

    [[nodiscard]] QString archiveUrl() const;
};

[[nodiscard]] std::expected<ResolvedCorpusHead, QString> parseCorpusHeadResponse(
    const QByteArray &json, const QDateTime &checkedAt);
[[nodiscard]] QNetworkRequest corpusHeadRequest();

struct CorpusDownloadOptions {
    QNetworkAccessManager *network = nullptr; ///< non-owning test/application transport
    qsizetype maxDownloadBytes = 32 * 1024 * 1024;
    std::optional<ResolvedCorpusHead> resolvedHead; ///< deterministic/offline test seam
};

/// Downloads OP-songs/main, extracts it into an isolated temporary directory,
/// runs every ope-check gate, and only then installs it into the app data area.
class CorpusDownloadDialog : public QDialog {
    Q_OBJECT
public:
    explicit CorpusDownloadDialog(QString target, QWidget *parent = nullptr);
    CorpusDownloadDialog(
        QString target, CorpusDownloadOptions options, QWidget *parent = nullptr);

    [[nodiscard]] const corpus::InstallResult &installResult() const noexcept
    {
        return m_installResult;
    }
    [[nodiscard]] const cli::CheckSummary &checkSummary() const noexcept
    {
        return m_checkSummary;
    }
    [[nodiscard]] const ResolvedCorpusHead &resolvedHead() const noexcept { return m_head; }

public Q_SLOTS:
    void reject() override;

private:
    void start();
    void startHeadResolution();
    void headFinished();
    void startArchiveDownload();
    void downloadFinished();
    void fail(const QString &message);
    void setStage(const QString &message);

    QString m_target;
    QNetworkAccessManager m_ownedNetwork;
    QNetworkAccessManager *m_network = nullptr;
    qsizetype m_maxDownloadBytes = 0;
    std::optional<ResolvedCorpusHead> m_resolvedHeadOverride;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_headResponse;
    QByteArray m_download;
    QString m_failure;
    QString m_finalUrl;
    QString m_etag;
    QTemporaryDir m_temporary;
    corpus::InstallResult m_installResult;
    cli::CheckSummary m_checkSummary;
    ResolvedCorpusHead m_head;
    bool m_validating = false;
    bool m_cancelled = false;
    QLabel *m_stage = nullptr;
    QLabel *m_detail = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_cancel = nullptr;
};

} // namespace ope::ui
