#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

struct RemixRecord {
    QString ocrId;
    int ocrNumber = 0;
    QString title;
    QString game;
    QString artists;
    QString posted;
    QString pageUrl;
    QString thumbnailUrl;
    QString duration;
    QStringList mirrors;
    QString status;
    QString localPath;
    QString lastError;
};

struct AlbumRecord {
    int albumId = 0;
    QString slug;
    QString title;
    QString catalog;
    QString classification;
    QString released;
    QString pageUrl;
    QString torrentUrl;
    QString httpHomepage;
    QStringList zipMirrors;
    bool ocrPublished = false;
    QString status;
    QString localPath;
    QString lastError;
};

struct DownloadJob {
    enum Kind { Remix, AlbumZip, Torrent } kind = Remix;
    QString itemId;
    QString displayName;
    QString status;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    QString error;
};

Q_DECLARE_METATYPE(RemixRecord)
Q_DECLARE_METATYPE(AlbumRecord)
Q_DECLARE_METATYPE(DownloadJob)
Q_DECLARE_METATYPE(QVector<RemixRecord>)
Q_DECLARE_METATYPE(QVector<AlbumRecord>)
