#pragma once

#include "Types.h"

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

class CatalogStore : public QObject {
    Q_OBJECT
public:
    explicit CatalogStore(QObject *parent = nullptr);

    bool open();
    bool isOpen() const { return m_open; }
    QString lastError() const { return m_lastError; }

    void upsertRemixes(const QVector<RemixRecord> &records);
    bool updateRemix(const RemixRecord &record);
    RemixRecord remix(const QString &ocrId) const;
    QVector<RemixRecord> remixes(const QString &search = {}) const;
    QVector<RemixRecord> remixesNeedingDownload() const;
    int remixCount() const;
    int remixDownloadedCount() const;
    int remixTorrentCoveredCount() const;
    int remixNeedingCount() const;
    void markRemixesInTorrentRange(int from, int to, const QString &note);

    void upsertAlbums(const QVector<AlbumRecord> &records);
    bool updateAlbum(const AlbumRecord &record);
    AlbumRecord album(int albumId) const;
    QVector<AlbumRecord> albums(const QString &search = {}) const;
    QVector<AlbumRecord> albumsNeedingDownload() const;
    int albumCount() const;
    int albumDownloadedCount() const;
    int albumTorrentCoveredCount() const;
    int albumNeedingCount() const;

    void setMeta(const QString &key, const QString &value);
    QString meta(const QString &key) const;

    void markExistingFiles(const QString &libraryPath);

private:
    bool exec(const QString &sql);
    QSqlDatabase db() const;
    RemixRecord remixFromQuery(const class QSqlQuery &q) const;
    AlbumRecord albumFromQuery(const class QSqlQuery &q) const;
    int countQuery(const QString &sql) const;
    static QString escapeLike(const QString &text);

    QString m_lastError;
    QString m_connectionName;
    bool m_open = false;
};
