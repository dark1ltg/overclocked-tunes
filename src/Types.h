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
    QString status; // unknown, available, downloaded, in-torrent, unavailable, queued, error
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
    enum Kind { Remix, AlbumZip, Torrent, CollectionTorrent } kind = Remix;
    QString itemId;
    QString displayName;
    QString status;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    QString error;
};

struct TorrentListing {
    QString name;
    QString url;
    QString category;
    QString added;
    int rangeStart = 0;
    int rangeEnd = 0;
    qint64 sizeMb = 0;
};

struct TorrentCoverage {
    QVector<TorrentListing> collections;
    QVector<TorrentListing> albums;
    int lastCoveredRemix = 0;
    int collectionTrackCount = 0;
};

Q_DECLARE_METATYPE(RemixRecord)
Q_DECLARE_METATYPE(AlbumRecord)
Q_DECLARE_METATYPE(DownloadJob)
Q_DECLARE_METATYPE(TorrentCoverage)
Q_DECLARE_METATYPE(QVector<RemixRecord>)
Q_DECLARE_METATYPE(QVector<AlbumRecord>)
