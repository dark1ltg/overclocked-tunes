#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <functional>

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

    Result get(const QString &url, int timeoutMs = 30000);
    Result downloadToFile(const QString &url, const QString &destPath, int timeoutMs = 0,
                          const std::function<void(qint64, qint64)> &progress = {});

private:
    QNetworkAccessManager m_nam;
};
