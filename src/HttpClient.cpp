#include "HttpClient.h"

#include "appinfo.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
{
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void HttpClient::abort()
{
    m_abortRequested = true;
    if (m_reply)
        m_reply->abort();
}

bool HttpClient::cancelled() const
{
    return m_abortRequested || (m_cancel && m_cancel->load());
}

void HttpClient::waitFor(QNetworkReply *reply, int timeoutMs)
{
    m_reply = reply;
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, &loop, [this, reply]() {
        if (cancelled())
            reply->abort();
    });
    poll.start();

    QTimer timeout;
    timeout.setSingleShot(true);
    if (timeoutMs > 0) {
        QObject::connect(&timeout, &QTimer::timeout, &loop, [reply, &loop]() {
            reply->abort();
            loop.quit();
        });
        timeout.start(timeoutMs);
    }
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    poll.stop();
    m_reply = nullptr;
}

HttpClient::Result HttpClient::get(const QString &url, int timeoutMs)
{
    Result result;
    if (cancelled()) {
        result.error = QStringLiteral("Cancelled");
        return result;
    }
    if (url.trimmed().isEmpty()) {
        result.error = QStringLiteral("Empty URL");
        return result;
    }

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(AppInfo::userAgent()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (timeoutMs > 0)
        req.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = m_nam.get(req);
    waitFor(reply, timeoutMs > 0 ? timeoutMs + 1000 : 0);

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.finalUrl = reply->url().toString();
    if (cancelled()) {
        result.error = QStringLiteral("Cancelled");
        result.ok = false;
    } else if (reply->error() != QNetworkReply::NoError && result.status != 404) {
        result.error = reply->errorString();
        result.ok = false;
    } else {
        result.body = reply->readAll();
        result.ok = (result.status >= 200 && result.status < 300);
        if (!result.ok && result.error.isEmpty())
            result.error = QStringLiteral("HTTP %1").arg(result.status);
    }
    reply->deleteLater();
    return result;
}

HttpClient::Result HttpClient::downloadToFile(const QString &url, const QString &destPath, int timeoutMs,
                                              const std::function<void(qint64, qint64)> &progress)
{
    Result result;
    if (cancelled()) {
        result.error = QStringLiteral("Cancelled");
        return result;
    }
    if (url.trimmed().isEmpty()) {
        result.error = QStringLiteral("Empty URL");
        return result;
    }

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(AppInfo::userAgent()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    const int idleTimeout = timeoutMs > 0 ? timeoutMs : 120000;
    req.setTransferTimeout(idleTimeout);

    if (destPath.trimmed().isEmpty() || destPath.endsWith(QLatin1Char('/'))
        || QFileInfo(destPath).isDir()) {
        result.error = QStringLiteral("Refusing to write to %1").arg(destPath);
        return result;
    }

    const QString tmpPath = destPath + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("Cannot write %1").arg(tmpPath);
        return result;
    }

    QNetworkReply *reply = m_nam.get(req);
    bool writeOk = true;
    auto writeChunk = [&](const QByteArray &chunk) {
        if (!writeOk || chunk.isEmpty())
            return;
        const qint64 n = file.write(chunk);
        if (n != chunk.size())
            writeOk = false;
    };
    QObject::connect(reply, &QNetworkReply::readyRead, this, [&]() { writeChunk(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, this, [&](qint64 done, qint64 total) {
        if (progress)
            progress(done, total);
    });
    waitFor(reply, 0);
    writeChunk(reply->readAll());
    if (!file.flush())
        writeOk = false;
    file.close();
    if (file.error() != QFile::NoError)
        writeOk = false;

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.finalUrl = reply->url().toString();
    if (cancelled()) {
        result.error = QStringLiteral("Cancelled");
        QFile::remove(tmpPath);
        reply->deleteLater();
        return result;
    }
    if (!writeOk) {
        result.error = QStringLiteral("Write failed for %1 (disk full?)").arg(tmpPath);
        QFile::remove(tmpPath);
        reply->deleteLater();
        return result;
    }
    if (reply->error() != QNetworkReply::NoError || result.status < 200 || result.status >= 300) {
        result.error = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                : QStringLiteral("HTTP %1").arg(result.status);
        QFile::remove(tmpPath);
        reply->deleteLater();
        return result;
    }

    if (QFile::exists(destPath) && !QFile::remove(destPath)) {
        result.error = QStringLiteral("Cannot replace %1").arg(destPath);
        reply->deleteLater();
        return result;
    }
    if (!QFile::rename(tmpPath, destPath) && !QFile::copy(tmpPath, destPath)) {
        result.error = QStringLiteral("Cannot finalize %1").arg(destPath);
        reply->deleteLater();
        return result;
    }
    QFile::remove(tmpPath);
    result.ok = true;
    reply->deleteLater();
    return result;
}
