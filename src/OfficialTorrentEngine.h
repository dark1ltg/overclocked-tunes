#pragma once

#include "Types.h"

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
    bool addOfficialTorrent(const QString &sourceUrl, const QString &torrentFile, const QString &savePath);
    void poll();

signals:
    void jobUpdated(const DownloadJob &job);
    void torrentFailed(const QString &name, const QString &error);

private:
#ifdef OCRCATALOG_HAS_TORRENT
    std::unique_ptr<libtorrent::session> m_session;
    QTimer *m_timer = nullptr;
#endif
};
