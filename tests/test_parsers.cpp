#include "HtmlParsers.h"

#include <QFile>
#include <QTextStream>
#include <cstdlib>

#ifdef OCR_HAS_QT_TEST
#include <QtTest/QTest>
#endif

static QString loadFixture(const char *name)
{
    QFile f(QStringLiteral(OCR_FIXTURE_DIR) + QLatin1Char('/') + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

static int fail(const QString &msg)
{
    QTextStream(stderr) << "FAIL: " << msg << Qt::endl;
    return 1;
}

int main(int, char **)
{
    const QString remixList = loadFixture("remix_list.html");
    if (remixList.isEmpty())
        return fail(QStringLiteral("missing remix_list.html"));
    const auto remixes = HtmlParsers::parseRemixList(remixList);
    if (remixes.size() < 1)
        return fail(QStringLiteral("expected remixes, got %1").arg(remixes.size()));
    if (remixes.first().ocrId != QStringLiteral("OCR05089"))
        return fail(QStringLiteral("first remix id %1").arg(remixes.first().ocrId));
    if (remixes.first().title != QStringLiteral("Psy Want It That Way"))
        return fail(QStringLiteral("first title %1").arg(remixes.first().title));
    if (!remixes.first().game.contains(QStringLiteral("Pok")) || !remixes.first().game.contains(QStringLiteral("Blue Version")))
        return fail(QStringLiteral("first game %1").arg(remixes.first().game));
    if (!remixes.first().artists.contains(QStringLiteral("Emunator")))
        return fail(QStringLiteral("artists %1").arg(remixes.first().artists));
    if (HtmlParsers::parseListedCount(remixList, QStringLiteral("ReMixes")) != 4892)
        return fail(QStringLiteral("remix count"));
    if (HtmlParsers::parseLastOffset(remixList) != 4890)
        return fail(QStringLiteral("last offset %1").arg(HtmlParsers::parseLastOffset(remixList)));

    const QString remixDetail = loadFixture("remix_detail.html");
    const auto detail = HtmlParsers::parseRemixDetail(remixDetail);
    if (detail.mirrors.size() < 2)
        return fail(QStringLiteral("mirrors %1").arg(detail.mirrors.size()));
    if (!detail.mirrors.first().contains(QStringLiteral("ocr.blueblue.fr")))
        return fail(QStringLiteral("preferred mirror %1").arg(detail.mirrors.first()));
    if (detail.duration != QStringLiteral("2:59"))
        return fail(QStringLiteral("duration %1").arg(detail.duration));

    const QString albumList = loadFixture("album_list.html");
    const auto albums = HtmlParsers::parseAlbumList(albumList);
    if (albums.size() < 1)
        return fail(QStringLiteral("albums %1").arg(albums.size()));
    AlbumRecord sample;
    for (const auto &a : albums) {
        if (a.catalog == QStringLiteral("OCRI-0016"))
            sample = a;
    }
    if (sample.catalog.isEmpty())
        sample = albums.first();
    if (!sample.title.contains(QStringLiteral("Fall")) && !sample.title.contains(QStringLiteral("Rise"))
        && sample.catalog.isEmpty())
        return fail(QStringLiteral("album title %1").arg(sample.title));
    if (!sample.ocrPublished)
        return fail(QStringLiteral("expected OCR published"));
    if (HtmlParsers::parseListedCount(albumList, QStringLiteral("Albums")) != 153)
        return fail(QStringLiteral("album count"));

    const QString albumDetail = loadFixture("album_detail.html");
    AlbumRecord album;
    HtmlParsers::parseAlbumDetail(&album, albumDetail);
    if (!album.torrentUrl.contains(QStringLiteral("Balance_and_Ruin.torrent")))
        return fail(QStringLiteral("torrent %1").arg(album.torrentUrl));
    if (!album.httpHomepage.contains(QStringLiteral("ff6.ocremix.org")))
        return fail(QStringLiteral("homepage %1").arg(album.httpHomepage));

    const QString home = loadFixture("album_homepage.html");
    const auto zips = HtmlParsers::parseAlbumZipMirrors(home);
    if (zips.size() < 3)
        return fail(QStringLiteral("zips %1").arg(zips.size()));
    if (!zips.join(QLatin1Char(' ')).contains(QLatin1String(".zip")))
        return fail(QStringLiteral("zip urls"));

    const QString torrentsHtml = loadFixture("torrents.html");
    if (torrentsHtml.isEmpty())
        return fail(QStringLiteral("missing torrents.html"));
    const auto torrentList = HtmlParsers::parseTorrentList(torrentsHtml);
    const auto coverage = HtmlParsers::summarizeTorrentCoverage(torrentList);
    if (coverage.collections.size() < 2)
        return fail(QStringLiteral("collection torrents %1").arg(coverage.collections.size()));
    if (coverage.lastCoveredRemix != 4473)
        return fail(QStringLiteral("last covered OCR %1").arg(coverage.lastCoveredRemix));
    if (coverage.collectionTrackCount < 4473)
        return fail(QStringLiteral("collection tracks %1").arg(coverage.collectionTrackCount));
    AlbumRecord fall;
    fall.title = QStringLiteral("Fall & Rise");
    fall.catalog = QStringLiteral("OCRI-0016");
    const auto *fallTorrent = HtmlParsers::matchAlbumTorrent(fall, torrentList);
    if (!fallTorrent || !fallTorrent->url.contains(QStringLiteral("Fall")))
        return fail(QStringLiteral("album torrent match"));
    if (!HtmlParsers::preferHttps(QStringLiteral("http://bt.ocremix.org/torrents/x.torrent"))
             .startsWith(QLatin1String("https://")))
        return fail(QStringLiteral("prefer https"));

    QTextStream(stdout) << "parser tests OK (" << remixes.size() << " remixes, "
                        << albums.size() << " albums, torrents through OCR"
                        << coverage.lastCoveredRemix << ")" << Qt::endl;
    return 0;
}
