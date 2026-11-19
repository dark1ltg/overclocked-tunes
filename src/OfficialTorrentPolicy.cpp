#include "OfficialTorrentPolicy.h"

#include <QUrl>

namespace OfficialTorrentPolicy {
namespace {

QString hostOf(const QString &url)
{
    QUrl parsed(url);
    QString host = parsed.host().toLower();
    if (!host.isEmpty())
        return host;

    QString trimmed = url.trimmed().toLower();
    const int scheme = trimmed.indexOf(QLatin1String("://"));
    if (scheme < 0)
        return {};
    QString rest = trimmed.mid(scheme + 3);
    if (rest.startsWith(QLatin1Char('['))) {
        const int end = rest.indexOf(QLatin1Char(']'));
        if (end < 0)
            return {};
        return rest.mid(1, end - 1);
    }
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        rest = rest.left(slash);
    const int colon = rest.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        rest = rest.left(colon);
    return rest;
}

bool hostIsOcremix(const QString &host)
{
    return host == QLatin1String("bt.ocremix.org") || host == QLatin1String("ocremix.org")
        || host.endsWith(QLatin1String(".ocremix.org"));
}

} // namespace

bool isOfficialTorrentUrl(const QString &url)
{
    const QUrl parsed(url);
    if (!parsed.isValid())
        return false;
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http"))
        return false;
    if (parsed.host().compare(QLatin1String("bt.ocremix.org"), Qt::CaseInsensitive) != 0)
        return false;
    const QString path = parsed.path();
    return path.startsWith(QLatin1String("/torrents/"), Qt::CaseInsensitive)
        && path.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive);
}

bool isOfficialTrackerUrl(const QString &url)
{
    const QString scheme = QUrl(url).scheme().toLower();
    if (!scheme.isEmpty() && scheme != QLatin1String("http") && scheme != QLatin1String("https")
        && scheme != QLatin1String("udp"))
        return false;
    return hostIsOcremix(hostOf(url));
}

} // namespace OfficialTorrentPolicy
