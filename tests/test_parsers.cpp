#include "HtmlParsers.h"
#include "OfficialTorrentPolicy.h"

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

    const auto starters = HtmlParsers::startingCollectionTorrents();
    if (starters.size() != 2)
        return fail(QStringLiteral("starting collections %1").arg(starters.size()));
    if (!starters[0].url.contains(QStringLiteral("1_to_4000")) || starters[0].rangeEnd != 4000)
        return fail(QStringLiteral("first starting torrent %1").arg(starters[0].url));
    if (!starters[1].url.contains(QStringLiteral("4001_to_4473")) || starters[1].rangeStart != 4001
        || starters[1].rangeEnd != 4473)
        return fail(QStringLiteral("second starting torrent %1").arg(starters[1].url));

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

    const QString officialTorrent =
        QStringLiteral("https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_1_to_4000_[v20201028].torrent");
    if (!OfficialTorrentPolicy::isOfficialTorrentUrl(officialTorrent))
        return fail(QStringLiteral("official collection torrent url"));
    if (OfficialTorrentPolicy::isOfficialTorrentUrl(QStringLiteral("https://example.com/torrents/other.torrent")))
        return fail(QStringLiteral("rejected foreign torrent host"));
    if (OfficialTorrentPolicy::isOfficialTorrentUrl(QStringLiteral("magnet:?xt=urn:btih:abc")))
        return fail(QStringLiteral("rejected magnet"));
    if (OfficialTorrentPolicy::isOfficialTorrentUrl(
            QStringLiteral("https://bt.ocremix.org/files/not-a-torrent.bin")))
        return fail(QStringLiteral("rejected non-torrent path"));
    if (!OfficialTorrentPolicy::isOfficialTrackerUrl(QStringLiteral("https://bt.ocremix.org/announce.php")))
        return fail(QStringLiteral("official https tracker"));
    if (!OfficialTorrentPolicy::isOfficialTrackerUrl(QStringLiteral("udp://bt.ocremix.org:2710/announce")))
        return fail(QStringLiteral("official udp tracker"));
    if (OfficialTorrentPolicy::isOfficialTrackerUrl(QStringLiteral("udp://tracker.opentrackr.org:1337/announce")))
        return fail(QStringLiteral("rejected third-party tracker"));
    if (OfficialTorrentPolicy::isOfficialTrackerUrl(QStringLiteral("udp://[2001:db8::1]:2710/announce")))
        return fail(QStringLiteral("rejected ipv6 third-party tracker"));
    if (!OfficialTorrentPolicy::isOfficialTrackerUrl(QStringLiteral("http://bt.ocremix.org/announce")))
        return fail(QStringLiteral("official ipv4 tracker"));

    TorrentListing fromUrl;
    fromUrl.url = QStringLiteral("https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_4474_to_5000.torrent");
    HtmlParsers::fillCollectionRange(&fromUrl);
    if (fromUrl.rangeStart != 4474 || fromUrl.rangeEnd != 5000)
        return fail(QStringLiteral("range from url %1-%2").arg(fromUrl.rangeStart).arg(fromUrl.rangeEnd));

    const QString sixDigit = QStringLiteral(
        "<tr><td><a class=\"area-link\" href=\"/remix/OCR100000\">x</a>"
        "<a class=\"main\" href=\"/remix/OCR100000\">Six Digit</a>"
        "<a href=\"/game/1/test\">Test Game</a></td></tr>");
    const auto six = HtmlParsers::parseRemixList(sixDigit);
    if (six.size() != 1 || six.first().ocrId != QStringLiteral("OCR100000") || six.first().ocrNumber != 100000)
        return fail(QStringLiteral("six digit ocr id"));

    QTextStream(stdout) << "parser tests OK (" << remixes.size() << " remixes, "
                        << albums.size() << " albums, torrents through OCR"
                        << coverage.lastCoveredRemix << ")" << Qt::endl;
    return 0;
}
