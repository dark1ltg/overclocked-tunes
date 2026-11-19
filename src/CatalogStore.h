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
    QString lastError() const { return m_lastError; }

    void upsertRemixes(const QVector<RemixRecord> &records);
    void updateRemix(const RemixRecord &record);
    RemixRecord remix(const QString &ocrId) const;
    QVector<RemixRecord> remixes(const QString &search = {}) const;
    QVector<RemixRecord> remixesNeedingDownload() const;
    int remixCount() const;
    int remixDownloadedCount() const;

    void upsertAlbums(const QVector<AlbumRecord> &records);
    void updateAlbum(const AlbumRecord &record);
    AlbumRecord album(int albumId) const;
    QVector<AlbumRecord> albums(const QString &search = {}) const;
    QVector<AlbumRecord> albumsNeedingDownload() const;
    int albumCount() const;
    int albumDownloadedCount() const;

    void setMeta(const QString &key, const QString &value);
    QString meta(const QString &key) const;

    void markExistingFiles(const QString &libraryPath);

private:
    bool exec(const QString &sql);
    QSqlDatabase db() const;
    RemixRecord remixFromQuery(const class QSqlQuery &q) const;
    AlbumRecord albumFromQuery(const class QSqlQuery &q) const;

    QString m_lastError;
    QString m_connectionName;
};
