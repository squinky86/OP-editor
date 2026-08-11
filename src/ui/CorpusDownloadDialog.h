// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#pragma once

#include "cli/Cli.h"
#include "core/CorpusArchive.h"

#include <QDialog>
#include <QNetworkAccessManager>
#include <QTemporaryDir>

QT_BEGIN_NAMESPACE
class QLabel;
class QNetworkReply;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

namespace ope::ui {

/// Downloads OP-songs/main, extracts it into an isolated temporary directory,
/// runs every ope-check gate, and only then installs it into the app data area.
class CorpusDownloadDialog : public QDialog {
    Q_OBJECT
public:
    explicit CorpusDownloadDialog(QString target, QWidget *parent = nullptr);

    [[nodiscard]] const corpus::InstallResult &installResult() const noexcept
    {
        return m_installResult;
    }
    [[nodiscard]] const cli::CheckSummary &checkSummary() const noexcept
    {
        return m_checkSummary;
    }

private:
    void start();
    void downloadFinished();
    void fail(const QString &message);
    void setStage(const QString &message);

    QString m_target;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_download;
    QString m_failure;
    QString m_finalUrl;
    QString m_etag;
    QTemporaryDir m_temporary;
    corpus::InstallResult m_installResult;
    cli::CheckSummary m_checkSummary;
    QLabel *m_stage = nullptr;
    QLabel *m_detail = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_cancel = nullptr;
};

} // namespace ope::ui
