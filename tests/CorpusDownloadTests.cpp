// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Fixtures.h"

#include "ui/CorpusDownloadDialog.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <zip.h>

#include <algorithm>
#include <cstring>

using namespace ope;
using namespace ope::fixtures;
using namespace ope::ui;

namespace {

struct Response {
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString errorText;
    int delayMs = 0; // negative means wait until abort()
};

class FakeReply final : public QNetworkReply {
public:
    FakeReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request,
        Response response,
        QObject *parent)
        : QNetworkReply(parent)
        , m_response(std::move(response))
    {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        setRawHeader("ETag", "\"test-etag\"");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (m_response.error != NoError)
            setError(m_response.error, m_response.errorText);
        if (m_response.delayMs >= 0)
            QTimer::singleShot(m_response.delayMs, this, [this] { finish(); });
    }

    void abort() override
    {
        if (m_done)
            return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        if (!m_finishing)
            finish();
    }

    [[nodiscard]] qint64 bytesAvailable() const override
    {
        return m_response.body.size() - m_offset + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        if (m_offset >= m_response.body.size())
            return -1;
        const qint64 count
            = std::min<qint64>(maximum, m_response.body.size() - m_offset);
        std::memcpy(data, m_response.body.constData() + m_offset,
            static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    void finish()
    {
        if (m_done || m_finishing)
            return;
        m_finishing = true;
        if (m_offset < m_response.body.size())
            Q_EMIT readyRead();
        Q_EMIT downloadProgress(m_response.body.size(), m_response.body.size());
        setFinished(true);
        m_done = true;
        m_finishing = false;
        Q_EMIT finished();
    }

    Response m_response;
    qint64 m_offset = 0;
    bool m_finishing = false;
    bool m_done = false;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager {
public:
    explicit FakeNetworkAccessManager(Response response, QObject *parent = nullptr)
        : QNetworkAccessManager(parent)
        , m_response(std::move(response))
    {
    }

    int requests = 0;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
        QIODevice *outgoingData) override
    {
        Q_UNUSED(outgoingData)
        ++requests;
        return new FakeReply(operation, request, m_response, this);
    }

private:
    Response m_response;
};

bool makeZipEntries(const QString &path,
    const QList<QPair<QByteArray, QByteArray>> &entries, bool firstEntryIsSymlink = false)
{
    int error = 0;
    zip_t *archive = zip_open(QFile::encodeName(path).constData(), ZIP_CREATE | ZIP_TRUNCATE,
        &error);
    if (!archive)
        return false;
    for (qsizetype item = 0; item < entries.size(); ++item) {
        const auto &[name, contents] = entries.at(item);
        zip_source_t *source
            = zip_source_buffer(archive, contents.constData(), contents.size(), 0);
        const zip_int64_t index = source
            ? zip_file_add(archive, name.constData(), source, ZIP_FL_ENC_UTF_8)
            : -1;
        if (index < 0) {
            if (source)
                zip_source_free(source);
            zip_discard(archive);
            return false;
        }
        if (item == 0 && firstEntryIsSymlink
            && zip_file_set_external_attributes(archive,
                   static_cast<zip_uint64_t>(index), 0, ZIP_OPSYS_UNIX,
                   static_cast<zip_uint32_t>(0120777U << 16))
                != 0) {
            zip_discard(archive);
            return false;
        }
    }
    return zip_close(archive) == 0;
}

bool makeZip(const QString &path, const QByteArray &song)
{
    return makeZipEntries(path,
        { { QByteArray("OP-songs-main/42/song.toml"), song } });
}

QByteArray archiveBytes(QTemporaryDir &temporary, const QByteArray &song)
{
    const QString path = temporary.filePath(QStringLiteral("response.zip"));
    if (!makeZip(path, song))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool hasLabel(const QWidget &widget, const QString &text)
{
    const auto labels = widget.findChildren<QLabel *>();
    return std::any_of(labels.cbegin(), labels.cend(), [&text](const QLabel *label) {
        return label->text().contains(text, Qt::CaseInsensitive);
    });
}

QPushButton *buttonWithText(QWidget &widget, const QString &text)
{
    for (QPushButton *button : widget.findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

ResolvedCorpusHead testHead()
{
    return ResolvedCorpusHead { QString(40, u'a'),
        QDateTime::fromString(QStringLiteral("2026-08-10T12:34:56Z"), Qt::ISODate),
        QDateTime::fromString(QStringLiteral("2026-08-11T13:00:00Z"), Qt::ISODate) };
}

} // namespace

class CorpusDownloadTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parsesResolvedHeadAndBuildsImmutableArchiveUrl()
    {
        const QDateTime checked
            = QDateTime::fromString(QStringLiteral("2026-08-11T14:00:00Z"), Qt::ISODate);
        const QByteArray json = QByteArray(R"({
            "sha": "0123456789abcdef0123456789abcdef01234567",
            "commit": { "committer": { "date": "2026-08-10T12:34:56Z" } }
        })");
        const auto parsed = parseCorpusHeadResponse(json, checked);
        const QString error = parsed ? QString() : parsed.error();
        QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(parsed->sha, QStringLiteral("0123456789abcdef0123456789abcdef01234567"));
        QCOMPARE(parsed->checkedAt, checked);
        QVERIFY(parsed->archiveUrl().endsWith(parsed->sha));

        QVERIFY(!parseCorpusHeadResponse(QByteArray("{}"), checked));
        QVERIFY(!parseCorpusHeadResponse(QByteArray("not json"), checked));
    }

    void networkFailuresLeaveInstalledCorpusUntouched_data()
    {
        QTest::addColumn<int>("error");
        QTest::addColumn<QString>("message");
        QTest::newRow("offline") << static_cast<int>(QNetworkReply::HostNotFoundError)
                                  << QStringLiteral("host not found");
        QTest::newRow("tls") << static_cast<int>(QNetworkReply::SslHandshakeFailedError)
                              << QStringLiteral("TLS handshake failed");
        QTest::newRow("proxy") << static_cast<int>(QNetworkReply::ProxyConnectionRefusedError)
                                << QStringLiteral("proxy refused");
    }

    void networkFailuresLeaveInstalledCorpusUntouched()
    {
        QFETCH(int, error);
        QFETCH(QString, message);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString target = temporary.filePath(QStringLiteral("installed"));
        const QString sentinel = QDir(target).filePath(QStringLiteral("sentinel.txt"));
        writeFile(sentinel, QByteArray("original\n"));

        FakeNetworkAccessManager network(
            Response { {}, static_cast<QNetworkReply::NetworkError>(error), message, 0 });
        CorpusDownloadOptions options;
        options.network = &network;
        CorpusDownloadDialog dialog(target, options);
        dialog.show();

        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(dialog, QStringLiteral("update failed")), 2000);
        QCOMPARE(network.requests, 1);
        QCOMPARE(readFile(sentinel), QByteArray("original\n"));
        QCOMPARE(QDir(temporary.path())
                     .entryList({ QStringLiteral("installed.backup-*") }, QDir::Dirs)
                     .size(),
            0);
    }

    void cancellationLeavesInstalledCorpusUntouched()
    {
        QTemporaryDir temporary;
        const QString target = temporary.filePath(QStringLiteral("installed"));
        const QString sentinel = QDir(target).filePath(QStringLiteral("sentinel.txt"));
        writeFile(sentinel, QByteArray("original\n"));

        FakeNetworkAccessManager network(Response { {}, QNetworkReply::NoError, {}, -1 });
        CorpusDownloadOptions options;
        options.network = &network;
        CorpusDownloadDialog dialog(target, options);
        dialog.show();
        QTRY_COMPARE_WITH_TIMEOUT(network.requests, 1, 1000);
        QPushButton *cancel = buttonWithText(dialog, QStringLiteral("Cancel"));
        QVERIFY(cancel);
        QTest::mouseClick(cancel, Qt::LeftButton);

        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
        QCOMPARE(readFile(sentinel), QByteArray("original\n"));
    }

    void oversizedDownloadLeavesInstalledCorpusUntouched()
    {
        QTemporaryDir temporary;
        const QString target = temporary.filePath(QStringLiteral("installed"));
        const QString sentinel = QDir(target).filePath(QStringLiteral("sentinel.txt"));
        writeFile(sentinel, QByteArray("original\n"));

        FakeNetworkAccessManager network(
            Response { QByteArray(65, 'x'), QNetworkReply::NoError, {}, 0 });
        CorpusDownloadOptions options;
        options.network = &network;
        options.maxDownloadBytes = 64;
        options.resolvedHead = testHead();
        CorpusDownloadDialog dialog(target, options);
        dialog.show();

        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(dialog, QStringLiteral("update failed")), 2000);
        QVERIFY(hasLabel(dialog, QStringLiteral("safety limit")));
        QCOMPARE(readFile(sentinel), QByteArray("original\n"));
    }

    void corruptOrInvalidCorpusLeavesInstalledCorpusUntouched_data()
    {
        QTest::addColumn<QByteArray>("song");
        QTest::newRow("corrupt TOML") << QByteArray("title = [\n");
        QTest::newRow("semantic validation error") << QByteArray("title = \"No music\"\n");
    }

    void rejectedArchiveLeavesInstalledCorpusUntouched_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("truncated") << QStringLiteral("truncated");
        QTest::newRow("traversal") << QStringLiteral("traversal");
        QTest::newRow("link") << QStringLiteral("link");
        QTest::newRow("multiple roots") << QStringLiteral("roots");
    }

    void rejectedArchiveLeavesInstalledCorpusUntouched()
    {
        QFETCH(QString, kind);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString archivePath = temporary.filePath(QStringLiteral("rejected.zip"));
        if (kind == QLatin1String("truncated")) {
            writeFile(archivePath, QByteArray("PK\x03\x04truncated", 13));
        } else if (kind == QLatin1String("traversal")) {
            QVERIFY(makeZipEntries(archivePath,
                { { QByteArray("OP-songs-main/1/../2/song.toml"), baseSong() } }));
        } else if (kind == QLatin1String("link")) {
            QVERIFY(makeZipEntries(archivePath,
                { { QByteArray("OP-songs-main/link"), QByteArray("../outside") } }, true));
        } else {
            QVERIFY(makeZipEntries(archivePath,
                { { QByteArray("OP-songs-main/42/song.toml"), baseSong() },
                    { QByteArray("unexpected/43/song.toml"), baseSong() } }));
        }
        const QByteArray zip = readFile(archivePath);
        QVERIFY(!zip.isEmpty());
        const QString target = temporary.filePath(QStringLiteral("installed"));
        const QString sentinel = QDir(target).filePath(QStringLiteral("sentinel.txt"));
        writeFile(sentinel, QByteArray("original\n"));

        FakeNetworkAccessManager network(
            Response { zip, QNetworkReply::NoError, {}, 0 });
        CorpusDownloadOptions options;
        options.network = &network;
        options.resolvedHead = testHead();
        CorpusDownloadDialog dialog(target, options);
        dialog.show();

        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(dialog, QStringLiteral("update failed")), 3000);
        QVERIFY(hasLabel(dialog, QStringLiteral("archive was rejected")));
        QCOMPARE(readFile(sentinel), QByteArray("original\n"));
        QCOMPARE(QDir(temporary.path())
                     .entryList({ QStringLiteral("installed.backup-*") }, QDir::Dirs)
                     .size(),
            0);
    }

    void corruptOrInvalidCorpusLeavesInstalledCorpusUntouched()
    {
        QFETCH(QByteArray, song);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QByteArray zip = archiveBytes(temporary, song);
        QVERIFY(!zip.isEmpty());
        const QString target = temporary.filePath(QStringLiteral("installed"));
        const QString sentinel = QDir(target).filePath(QStringLiteral("sentinel.txt"));
        writeFile(sentinel, QByteArray("original\n"));

        FakeNetworkAccessManager network(
            Response { zip, QNetworkReply::NoError, {}, 0 });
        CorpusDownloadOptions options;
        options.network = &network;
        options.resolvedHead = testHead();
        CorpusDownloadDialog dialog(target, options);
        dialog.show();

        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(dialog, QStringLiteral("update failed")), 3000);
        QVERIFY(hasLabel(dialog, QStringLiteral("integrity checking")));
        QCOMPARE(readFile(sentinel), QByteArray("original\n"));
        QCOMPARE(QDir(temporary.path())
                     .entryList({ QStringLiteral("installed.backup-*") }, QDir::Dirs)
                     .size(),
            0);
    }

    void validatedDownloadReplacesCorpusAndRetainsBackup()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QByteArray zip = archiveBytes(temporary, baseSong());
        QVERIFY(!zip.isEmpty());
        const QString target = temporary.filePath(QStringLiteral("installed"));
        const QString sentinel = QDir(target).filePath(QStringLiteral("sentinel.txt"));
        writeFile(sentinel, QByteArray("original\n"));

        FakeNetworkAccessManager network(
            Response { zip, QNetworkReply::NoError, {}, 0 });
        CorpusDownloadOptions options;
        options.network = &network;
        options.resolvedHead = testHead();
        CorpusDownloadDialog dialog(target, options);
        dialog.show();

        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(dialog, QStringLiteral("corpus is ready")), 5000);
        QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("42/song.toml"))), baseSong());
        QVERIFY(QFileInfo::exists(
            QDir(target).filePath(QStringLiteral(".openpsalm-snapshot.json"))));
        const auto snapshot = corpus::readSnapshot(target);
        QVERIFY(snapshot);
        QCOMPARE(snapshot->commitSha, testHead().sha);
        QCOMPARE(snapshot->commitDate, testHead().committedAt);
        QCOMPARE(snapshot->currentAsOf, testHead().checkedAt);
        QVERIFY(!dialog.installResult().backup.isEmpty());
        QCOMPARE(readFile(QDir(dialog.installResult().backup)
                              .filePath(QStringLiteral("sentinel.txt"))),
            QByteArray("original\n"));

        QPushButton *continueButton = buttonWithText(dialog, QStringLiteral("Continue"));
        QVERIFY(continueButton);
        // The window close path must also report success because installation
        // has already completed before this page is displayed.
        dialog.reject();
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    }
};

QTEST_MAIN(CorpusDownloadTests)
#include "CorpusDownloadTests.moc"
