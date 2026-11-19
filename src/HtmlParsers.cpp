#include "HtmlParsers.h"

#include "appinfo.h"

#include <QRegularExpression>
#include <QtGlobal>
#include <algorithm>

namespace HtmlParsers {
namespace {

QString firstCapture(const QString &text, const QRegularExpression &re)
{
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList allCaptures(const QString &text, const QRegularExpression &re, int group = 1)
{
    QStringList out;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        out.push_back(it.next().captured(group));
    return out;
}

QRegularExpression rx(const char *pattern,
                      QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption)
{
    return QRegularExpression(QString::fromUtf8(pattern), opts);
}

int mirrorRank(const QString &url)
{
    if (url.contains(QLatin1String("ocr.blueblue.fr")))
        return 0;
    if (url.contains(QLatin1String("ocr2.blueblue.fr")))
        return 1;
    if (url.contains(QLatin1String("iterations.org")))
        return 2;
    if (url.contains(QLatin1String("belthesar")))
        return 3;
    if (url.contains(QLatin1String("dreamhosters")))
        return 4;
    if (url.contains(QLatin1String("ocrmirror.org")))
        return 8;
    return 5;
}

} // namespace

QString unescape(QString text)
{
    QString prev;
    int guard = 0;
    while (text != prev && guard++ < 4) {
        prev = text;
        text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
        text.replace(QLatin1String("&amp;"), QLatin1String("&"));
        text.replace(QLatin1String("&quot;"), QLatin1String("\""));
        text.replace(QLatin1String("&#39;"), QLatin1String("'"));
        text.replace(QLatin1String("&apos;"), QLatin1String("'"));
        text.replace(QLatin1String("&lt;"), QLatin1String("<"));
        text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    }
    return text.trimmed();
}

QStringList preferMirrors(QStringList urls)
{
    urls.removeDuplicates();
    urls.erase(std::remove_if(urls.begin(), urls.end(),
                              [](const QString &u) { return u.trimmed().isEmpty(); }),
               urls.end());
    std::stable_sort(urls.begin(), urls.end(), [](const QString &a, const QString &b) {
        return mirrorRank(a) < mirrorRank(b);
    });
    return urls;
}

int parseListedCount(const QString &html, const QString &label)
{
    QRegularExpression re(QStringLiteral("%1 \\(([0-9,]+)\\)").arg(QRegularExpression::escape(label)));
    const QString raw = firstCapture(html, re);
    if (raw.isEmpty())
        return 0;
    bool ok = false;
    const int n = QString(raw).remove(QLatin1Char(',')).toInt(&ok);
    return ok ? n : 0;
}

int parseLastOffset(const QString &html)
{
    QRegularExpression re = rx(R"re(offset=(\d+)[^>]*title="last page")re");
    const QString raw = firstCapture(html, re);
    bool ok = false;
    const int n = raw.toInt(&ok);
    return ok ? n : 0;
}

QVector<RemixRecord> parseRemixList(const QString &html)
{
    QVector<RemixRecord> out;
    const QString marker = QStringLiteral("class=\"area-link\"");
    int pos = 0;
    QRegularExpression idRe = rx("/remix/(OCR(\\d{5}))");
    QRegularExpression titleRe = rx(R"re(class="main[^"]*"[^>]*>([^<]+)</a>)re");
    QRegularExpression gameRe = rx(R"re(/game/\d+/[^"]+"[^>]*>([^<]+)</a>)re");
    QRegularExpression artistRe = rx(R"re(/artist/\d+/[^"]+"[^>]*>([^<]+)</a>)re");
    QRegularExpression thumbRe = rx(R"re(src="(/thumbs/[^"]+)")re");
    QRegularExpression dateRe = rx(
        R"re(class="text-center[^"]*"[^>]*>\s*([A-Z]{3} \d{1,2})\s*<br>\s*<span class="featured">(\d{4})</span>)re");

    while (true) {
        const int markerAt = html.indexOf(marker, pos);
        if (markerAt < 0)
            break;
        const int trStart = html.lastIndexOf(QLatin1String("<tr"), markerAt);
        const int trEnd = html.indexOf(QLatin1String("</tr>"), markerAt);
        if (trStart < 0 || trEnd < 0)
            break;
        const QString row = html.mid(trStart, trEnd - trStart);
        pos = trEnd + 5;

        const auto idMatch = idRe.match(row);
        if (!idMatch.hasMatch())
            continue;

        RemixRecord r;
        r.ocrId = idMatch.captured(1);
        r.ocrNumber = idMatch.captured(2).toInt();
        r.title = unescape(firstCapture(row, titleRe));
        r.game = unescape(firstCapture(row, gameRe));
        r.artists = unescape(allCaptures(row, artistRe).join(QStringLiteral(", ")));
        r.thumbnailUrl = firstCapture(row, thumbRe);
        if (!r.thumbnailUrl.isEmpty() && r.thumbnailUrl.startsWith(QLatin1Char('/')))
            r.thumbnailUrl = QString::fromLatin1(AppInfo::siteRoot()) + r.thumbnailUrl;
        const auto dateMatch = dateRe.match(row);
        if (dateMatch.hasMatch())
            r.posted = dateMatch.captured(1) + QLatin1Char(' ') + dateMatch.captured(2);
        r.pageUrl = QStringLiteral("%1/remix/%2").arg(QLatin1String(AppInfo::siteRoot()), r.ocrId);
        r.status = QStringLiteral("available");
        out.push_back(r);
    }
    return out;
}

RemixRecord parseRemixDetail(const QString &html, const RemixRecord &seed)
{
    RemixRecord r = seed;
    QRegularExpression mirrorRe = rx(
        R"re(href="(https?://[^"]+/files/music/remixes/[^"]+\.mp3)")re",
        QRegularExpression::CaseInsensitiveOption);
    r.mirrors = preferMirrors(allCaptures(html, mirrorRe));

    QRegularExpression durationRe = rx(R"re(<span class="subtext">([^<]+)</span>)re");
    const QString duration = unescape(firstCapture(html, durationRe));
    if (!duration.isEmpty())
        r.duration = duration;

    if (r.title.isEmpty()) {
        QRegularExpression titleRe = rx(R"re("([^"]+)"\s*<span class="subtext">)re");
        r.title = unescape(firstCapture(html, titleRe));
    }
    if (r.ocrId.isEmpty()) {
        QRegularExpression idRe = rx("/remix/(OCR(\\d{5}))");
        const auto m = idRe.match(html);
        if (m.hasMatch()) {
            r.ocrId = m.captured(1);
            r.ocrNumber = m.captured(2).toInt();
        }
    }
    if (r.pageUrl.isEmpty() && !r.ocrId.isEmpty())
        r.pageUrl = QStringLiteral("%1/remix/%2").arg(QLatin1String(AppInfo::siteRoot()), r.ocrId);
    if (r.status.isEmpty())
        r.status = r.mirrors.isEmpty() ? QStringLiteral("unavailable") : QStringLiteral("available");
    return r;
}

QVector<AlbumRecord> parseAlbumList(const QString &html)
{
    QVector<AlbumRecord> out;
    const QString marker = QStringLiteral("class=\"area-link\"");
    int pos = 0;
    QRegularExpression idRe = rx(R"re(/album/(\d+)/([^"]+))re");
    QRegularExpression titleRe = rx(R"re(class="main[^"]*"[^>]*>([^<]+)</a>)re");
    QRegularExpression catalogRe = rx(R"re(OverClocked ReMix,\s*(OCR[AIC]-?\d+))re");
    QRegularExpression classRe = rx(R"re(class="optional">([^<]+)<br>)re");
    QRegularExpression dateRe = rx(
        R"re(class="text-center"[^>]*>\s*([A-Z]{3} \d{1,2})\s*<br>\s*<span class="featured">(\d{4})</span>)re");

    while (true) {
        const int markerAt = html.indexOf(marker, pos);
        if (markerAt < 0)
            break;
        const int trStart = html.lastIndexOf(QLatin1String("<tr"), markerAt);
        const int trEnd = html.indexOf(QLatin1String("</tr>"), markerAt);
        if (trStart < 0 || trEnd < 0)
            break;
        const QString row = html.mid(trStart, trEnd - trStart);
        pos = trEnd + 5;

        const auto idMatch = idRe.match(row);
        if (!idMatch.hasMatch())
            continue;

        AlbumRecord a;
        a.albumId = idMatch.captured(1).toInt();
        a.slug = unescape(idMatch.captured(2));
        a.title = unescape(firstCapture(row, titleRe));
        a.catalog = firstCapture(row, catalogRe);
        a.classification = unescape(firstCapture(row, classRe));
        const auto dateMatch = dateRe.match(row);
        if (dateMatch.hasMatch())
            a.released = dateMatch.captured(1) + QLatin1Char(' ') + dateMatch.captured(2);
        a.pageUrl = QStringLiteral("%1/album/%2/%3")
                        .arg(QLatin1String(AppInfo::siteRoot()))
                        .arg(a.albumId)
                        .arg(a.slug);
        a.ocrPublished = isOcrPublishedAlbum(a);
        a.status = a.ocrPublished ? QStringLiteral("available") : QStringLiteral("catalog-only");
        out.push_back(a);
    }
    return out;
}

void parseAlbumDetail(AlbumRecord *album, const QString &html)
{
    if (!album)
        return;
    QRegularExpression torrentRe = rx(
        R"re(href="(https?://bt\.ocremix\.org/torrents/[^"]+\.torrent)")re",
        QRegularExpression::CaseInsensitiveOption);
    album->torrentUrl = preferHttps(firstCapture(html, torrentRe));

    QRegularExpression httpRe = rx(
        R"re(href="(https?://[^"]+)"[^>]*>HTTP</a>)re",
        QRegularExpression::CaseInsensitiveOption);
    album->httpHomepage = firstCapture(html, httpRe);
}

QStringList parseAlbumZipMirrors(const QString &html)
{
    QRegularExpression zipRe = rx(
        R"re(href="(https?://[^"]+/files/music/albums/[^"]+\.zip)")re",
        QRegularExpression::CaseInsensitiveOption);
    return preferMirrors(allCaptures(html, zipRe));
}

bool isOcrPublishedAlbum(const AlbumRecord &album)
{
    const QString cat = album.catalog.toUpper();
    if (cat.startsWith(QLatin1String("OCRA-")) || cat.startsWith(QLatin1String("OCRI-"))
        || cat.startsWith(QLatin1String("OCRC-")))
        return true;
    return album.classification.contains(QLatin1String("OC ReMix Album"), Qt::CaseInsensitive);
}

QString preferHttps(QString url)
{
    if (url.startsWith(QLatin1String("http://bt.ocremix.org"), Qt::CaseInsensitive))
        url.replace(0, 4, QStringLiteral("https"));
    return url;
}

QString normalizeKey(QString text)
{
    text = unescape(text).toLower();
    text.replace(QLatin1String("&amp;"), QLatin1String("and"));
    text.replace(QLatin1Char('&'), QLatin1String("and"));
    const char *noise[] = {"series", "the", "album", "overclocked", "ocremix", "remix"};
    for (const char *word : noise)
        text.replace(QLatin1String(word), QString());
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c.isLetterOrNumber())
            out += c;
    }
    return out;
}

void fillCollectionRange(TorrentListing *listing)
{
    if (!listing)
        return;
    QRegularExpression re = rx(R"re((\d+)\s+to\s+(\d+))re");
    const auto m = re.match(listing->name);
    if (!m.hasMatch())
        return;
    listing->rangeStart = m.captured(1).toInt();
    listing->rangeEnd = m.captured(2).toInt();
    if (listing->category.isEmpty())
        listing->category = QStringLiteral("Collection");
}

QVector<TorrentListing> parseTorrentList(const QString &html)
{
    QVector<TorrentListing> out;
    QRegularExpression linkRe = rx(
        R"re(href="(https?://bt\.ocremix\.org/torrents/[^"]+\.torrent)"[^>]*>([^<]+)</a>)re",
        QRegularExpression::CaseInsensitiveOption);
    auto it = linkRe.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        TorrentListing t;
        t.url = preferHttps(unescape(m.captured(1)));
        t.name = unescape(m.captured(2));
        fillCollectionRange(&t);
        out.push_back(t);
    }

    const QString marker = QStringLiteral("id=\"torrents\"");
    int tableAt = html.indexOf(marker);
    if (tableAt < 0)
        tableAt = html.indexOf(QLatin1String("<tbody>"));
    if (tableAt >= 0) {
        QRegularExpression rowRe = rx(
            R"re(<tr>\s*<td><a class="main[^"]*" href="(https?://bt\.ocremix\.org/torrents/[^"]+\.torrent)">([^<]+)</a></td>\s*<td>([^<]+)</td>\s*<td class="optional">([^<]*)</td>[\s\S]*?<td><strong>([^<]+)</strong></td>)re",
            QRegularExpression::CaseInsensitiveOption);
        auto rows = rowRe.globalMatch(html.mid(tableAt));
        while (rows.hasNext()) {
            const auto m = rows.next();
            const QString url = preferHttps(unescape(m.captured(1)));
            TorrentListing *slot = nullptr;
            for (auto &existing : out) {
                if (existing.url == url) {
                    slot = &existing;
                    break;
                }
            }
            TorrentListing fresh;
            if (!slot) {
                out.push_back(fresh);
                slot = &out.last();
                slot->url = url;
            }
            slot->name = unescape(m.captured(2));
            slot->category = unescape(m.captured(3)).trimmed();
            slot->sizeMb = m.captured(4).trimmed().toLongLong();
            slot->added = unescape(m.captured(5)).trimmed();
            fillCollectionRange(slot);
        }
    }

    QVector<TorrentListing> unique;
    for (const auto &t : out) {
        bool seen = false;
        for (const auto &u : unique) {
            if (u.url == t.url) {
                seen = true;
                break;
            }
        }
        if (!seen && !t.url.isEmpty())
            unique.push_back(t);
    }
    return unique;
}

TorrentCoverage summarizeTorrentCoverage(const QVector<TorrentListing> &listings)
{
    TorrentCoverage coverage;
    for (const auto &t : listings) {
        if (t.rangeEnd > 0 || t.category.compare(QLatin1String("Collection"), Qt::CaseInsensitive) == 0) {
            coverage.collections.push_back(t);
            if (t.rangeEnd > coverage.lastCoveredRemix)
                coverage.lastCoveredRemix = t.rangeEnd;
            if (t.rangeEnd >= t.rangeStart && t.rangeEnd > 0)
                coverage.collectionTrackCount += t.rangeEnd - t.rangeStart + 1;
        } else if (t.category.compare(QLatin1String("Albums"), Qt::CaseInsensitive) == 0
                   || t.category.isEmpty()) {
            coverage.albums.push_back(t);
        }
    }
    return coverage;
}

const TorrentListing *matchAlbumTorrent(const AlbumRecord &album, const QVector<TorrentListing> &listings)
{
    const QString albumKey = normalizeKey(album.title);
    const QString catalogKey = normalizeKey(album.catalog);
    const TorrentListing *best = nullptr;
    int bestScore = 0;
    for (const auto &t : listings) {
        if (t.rangeEnd > 0)
            continue;
        const QString key = normalizeKey(t.name);
        if (key.isEmpty())
            continue;
        int score = 0;
        if (!albumKey.isEmpty() && (key == albumKey || key.contains(albumKey) || albumKey.contains(key)))
            score = qMin(albumKey.size(), key.size());
        if (!catalogKey.isEmpty() && key.contains(catalogKey))
            score = qMax(score, catalogKey.size() + 10);
        if (score > bestScore) {
            bestScore = score;
            best = &t;
        }
    }
    return bestScore >= 6 ? best : nullptr;
}

} // namespace HtmlParsers
