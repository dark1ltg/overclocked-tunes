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

HttpClient::Result HttpClient::get(const QString &url, int timeoutMs)
{
    Result result;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(AppInfo::userAgent()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = m_nam.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (timeoutMs > 0)
        timeout.start(timeoutMs + 1000);
    loop.exec();

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.finalUrl = reply->url().toString();
    if (reply->error() != QNetworkReply::NoError && result.status != 404) {
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
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(AppInfo::userAgent()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (timeoutMs > 0)
        req.setTransferTimeout(timeoutMs);

    const QString tmpPath = destPath + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("Cannot write %1").arg(tmpPath);
        return result;
    }

    QNetworkReply *reply = m_nam.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() { file.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 done, qint64 total) {
        if (progress)
            progress(done, total);
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    file.write(reply->readAll());
    file.close();

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.finalUrl = reply->url().toString();
    if (reply->error() != QNetworkReply::NoError || result.status < 200 || result.status >= 300) {
        result.error = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                : QStringLiteral("HTTP %1").arg(result.status);
        QFile::remove(tmpPath);
        reply->deleteLater();
        return result;
    }

    QFile::remove(destPath);
    if (!QFile::rename(tmpPath, destPath)) {
        result.error = QStringLiteral("Cannot finalize %1").arg(destPath);
        QFile::remove(tmpPath);
        reply->deleteLater();
        return result;
    }
    result.ok = true;
    reply->deleteLater();
    return result;
}
