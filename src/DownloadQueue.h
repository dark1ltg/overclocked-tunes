#pragma once

#include "Types.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>

class DownloadQueue : public QObject {
    Q_OBJECT
public:
    explicit DownloadQueue(QObject *parent = nullptr);

    void setLibraryPath(const QString &path) { m_libraryPath = path; }
    void setDelayMs(int ms) { m_delayMs = ms; }
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
    void queueProgress(int done, int total, const QString &message);
    void finished(bool ok, const QString &message);

private:
    bool downloadRemix(RemixRecord record);
    bool downloadAlbum(AlbumRecord record);
    QString filenameFromUrl(const QString &url) const;

    QString m_libraryPath;
    int m_delayMs = 400;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
    QVector<RemixRecord> m_remixes;
    QVector<AlbumRecord> m_albums;
};
