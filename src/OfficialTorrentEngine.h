#pragma once

#include "Types.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <memory>

class QTimer;

#ifdef OCRCATALOG_HAS_TORRENT
namespace libtorrent {
class session;
}
#endif

class OfficialTorrentEngine : public QObject {
    Q_OBJECT
public:
    explicit OfficialTorrentEngine(QObject *parent = nullptr);
    ~OfficialTorrentEngine() override;

    static bool available();

public slots:
    void start();
    void stop();
    void cancelActive();
    bool addOfficialTorrent(const QString &sourceUrl, const QString &torrentFile, const QString &savePath,
                            const QString &itemId = {});
    bool hasActiveTorrents() const;
    void poll();

signals:
    void jobUpdated(const DownloadJob &job);
    void torrentFailed(const QString &name, const QString &error);
    void torrentCompleted(const QString &name, const QString &savePath);

private:
#ifdef OCRCATALOG_HAS_TORRENT
    void writeResumeFile(const QString &savePath, const QString &hashHex, const std::vector<char> &buf);
    QString resumePath(const QString &savePath, const QString &hashHex) const;
    QString jobIdForName(const QString &name) const;
    std::unique_ptr<libtorrent::session> m_session;
    QTimer *m_timer = nullptr;
    QHash<QString, QString> m_jobIds;
    int m_pendingResume = 0;
    bool m_stopped = false;
#endif
};
