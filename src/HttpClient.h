#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>

class QNetworkReply;

class HttpClient : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        int status = 0;
        QByteArray body;
        QString error;
        QString finalUrl;
    };

    explicit HttpClient(QObject *parent = nullptr);

    void setCancelFlag(std::atomic<bool> *flag) { m_cancel = flag; }
    void abort();
    void resetAbort();

    Result get(const QString &url, int timeoutMs = 30000);
    Result downloadToFile(const QString &url, const QString &destPath, int timeoutMs = 120000,
                          const std::function<void(qint64, qint64)> &progress = {});

private:
    bool cancelled() const;
    void waitFor(QNetworkReply *reply, int timeoutMs);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    std::atomic<bool> *m_cancel = nullptr;
    bool m_abortRequested = false;
};
