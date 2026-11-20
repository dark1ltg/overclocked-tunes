#pragma once

#include "JobGate.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

class OfficialTorrentEngine;

class DownloadQueue : public QObject {
    Q_OBJECT
public:
    explicit DownloadQueue(QObject *parent = nullptr);

    void setLibraryPath(const QString &path) { m_libraryPath = path; }
    void setDelayMs(int ms) { m_delayMs = ms; }
    void setConcurrency(int value) { m_concurrency = value; }
    void setPreferTorrents(bool value) { m_preferTorrents = value; }
    void setIncludeAlbums(bool value) { m_includeAlbums = value; }
    void setDownloadInApp(bool value) { m_downloadInApp = value; }
    void setOpenTorrents(bool value) { m_openTorrents = value; }
    void setTorrentEngine(OfficialTorrentEngine *engine) { m_engine = engine; }
    void setJobGate(JobGate *gate) { m_gate = gate; }
    void enqueueRemixes(const QVector<RemixRecord> &records);
    void enqueueAlbums(const QVector<AlbumRecord> &records);
    void cancel() { m_cancel = true; }
    bool isRunning() const { return m_running; }

public slots:
    void start();

signals:
    void jobUpdated(const DownloadJob &job);
    void remixUpdated(const RemixRecord &record);
    void albumUpdated(const AlbumRecord &record);
    void remixRangeCovered(int from, int to, const QString &note);
    void torrentsReady(const QStringList &paths);
    void coverageSummary(int lastOcr, int collectionTracks, int remainingRemixes, int remainingAlbums);
    void queueProgress(int done, int total, const QString &message);
    void finished(bool ok, const QString &message);

private:
    bool applyOfficialTorrents();
    bool downloadRemix(RemixRecord record);
    bool downloadAlbum(AlbumRecord record);
    void downloadRemixes();
    void waitForTorrents();
    QString filenameFromUrl(const QString &url) const;
    QString destForUrl(const QString &dir, const QString &url) const;
    QString torrentDir() const;
    QString collectionDir() const;
    QString albumDir() const;
    bool maybeStartInAppTorrent(const QString &sourceUrl, const QString &torrentFile, const QString &savePath);
    QString torrentJobId(const QString &url) const;

    QString m_libraryPath;
    int m_delayMs = 400;
    int m_concurrency = 1;
    bool m_preferTorrents = true;
    bool m_includeAlbums = true;
    bool m_downloadInApp = true;
    bool m_openTorrents = false;
    JobGate *m_gate = nullptr;
    OfficialTorrentEngine *m_engine = nullptr;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
    QVector<RemixRecord> m_remixes;
    QVector<AlbumRecord> m_albums;
};
