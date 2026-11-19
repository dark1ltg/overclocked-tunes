#pragma once

#include "Types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace HtmlParsers {

QString unescape(QString text);
QStringList preferMirrors(QStringList urls);

int parseListedCount(const QString &html, const QString &label);
int parseLastOffset(const QString &html);

QVector<RemixRecord> parseRemixList(const QString &html);
RemixRecord parseRemixDetail(const QString &html, const RemixRecord &seed = {});

QVector<AlbumRecord> parseAlbumList(const QString &html);
void parseAlbumDetail(AlbumRecord *album, const QString &html);
QStringList parseAlbumZipMirrors(const QString &html);

bool isOcrPublishedAlbum(const AlbumRecord &album);

QString preferHttps(QString url);
QString normalizeKey(QString text);
void fillCollectionRange(TorrentListing *listing);
QVector<TorrentListing> parseTorrentList(const QString &html);

inline QVector<TorrentListing> startingCollectionTorrents()
{
    QVector<TorrentListing> packs;

    TorrentListing first;
    first.name = QStringLiteral("OC ReMix Collection - 1 to 4000 [v20201028]");
    first.url = QStringLiteral(
        "https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_1_to_4000_[v20201028].torrent");
    first.category = QStringLiteral("Collection");
    fillCollectionRange(&first);
    packs.push_back(first);

    TorrentListing second;
    second.name = QStringLiteral("OC ReMix Collection - 4001 to 4473 (Q2 2023 update)");
    second.url = QStringLiteral(
        "https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_4001_to_4473_(Q2_2023_update).torrent");
    second.category = QStringLiteral("Collection");
    fillCollectionRange(&second);
    packs.push_back(second);

    return packs;
}
TorrentCoverage summarizeTorrentCoverage(const QVector<TorrentListing> &listings);
const TorrentListing *matchAlbumTorrent(const AlbumRecord &album, const QVector<TorrentListing> &listings);

} // namespace HtmlParsers
