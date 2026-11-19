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
TorrentCoverage summarizeTorrentCoverage(const QVector<TorrentListing> &listings);
const TorrentListing *matchAlbumTorrent(const AlbumRecord &album, const QVector<TorrentListing> &listings);

} // namespace HtmlParsers
