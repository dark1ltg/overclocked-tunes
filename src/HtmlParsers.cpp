#include "HtmlParsers.h"

#include "appinfo.h"

#include <QRegularExpression>
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
    album->torrentUrl = firstCapture(html, torrentRe);

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

} // namespace HtmlParsers
