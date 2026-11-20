#include "CatalogStore.h"
#include "HtmlParsers.h"
#include "HttpClient.h"
#include "MainWindow.h"
#include "Settings.h"
#include "Types.h"
#include "appinfo.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include <QTextStream>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

int runSmoke()
{
    QTextStream out(stdout);
    HttpClient http;

    const auto list = http.get(QStringLiteral("https://ocremix.org/remixes/?offset=0&sort=datedesc"));
    if (!list.ok) {
        out << "FAIL remix list: " << list.error << Qt::endl;
        return 1;
    }
    const auto remixes = HtmlParsers::parseRemixList(QString::fromUtf8(list.body));
    if (remixes.size() < 10) {
        out << "FAIL remix list count " << remixes.size() << Qt::endl;
        return 1;
    }
    out << "OK remix list " << remixes.size() << " items, first " << remixes.first().ocrId << Qt::endl;

    const auto detail = http.get(QStringLiteral("https://ocremix.org/remix/OCR00001"));
    if (!detail.ok) {
        out << "FAIL remix detail: " << detail.error << Qt::endl;
        return 1;
    }
    const auto parsed = HtmlParsers::parseRemixDetail(QString::fromUtf8(detail.body));
    if (parsed.mirrors.size() < 2) {
        out << "FAIL remix mirrors " << parsed.mirrors.size() << Qt::endl;
        return 1;
    }
    out << "OK remix mirrors " << parsed.mirrors.size() << " " << parsed.mirrors.first() << Qt::endl;

    const auto albumsHtml = http.get(QStringLiteral("https://ocremix.org/albums/"));
    if (!albumsHtml.ok) {
        out << "FAIL album list: " << albumsHtml.error << Qt::endl;
        return 1;
    }
    const auto albums = HtmlParsers::parseAlbumList(QString::fromUtf8(albumsHtml.body));
    if (albums.isEmpty()) {
        out << "FAIL album list empty" << Qt::endl;
        return 1;
    }
    out << "OK album list " << albums.size() << " " << albums.first().title << Qt::endl;

    AlbumRecord album;
    album.pageUrl = QStringLiteral("https://ocremix.org/album/46");
    const auto albumPage = http.get(album.pageUrl);
    if (!albumPage.ok) {
        out << "FAIL album detail: " << albumPage.error << Qt::endl;
        return 1;
    }
    HtmlParsers::parseAlbumDetail(&album, QString::fromUtf8(albumPage.body));
    if (album.torrentUrl.isEmpty() || album.httpHomepage.isEmpty()) {
        out << "FAIL album download links torrent=" << album.torrentUrl
            << " http=" << album.httpHomepage << Qt::endl;
        return 1;
    }
    const auto home = http.get(album.httpHomepage);
    if (!home.ok) {
        out << "FAIL album homepage: " << home.error << Qt::endl;
        return 1;
    }
    const auto zips = HtmlParsers::parseAlbumZipMirrors(QString::fromUtf8(home.body));
    if (zips.isEmpty()) {
        out << "FAIL album zip mirrors" << Qt::endl;
        return 1;
    }
    out << "OK album zip " << zips.first() << Qt::endl;

    const auto torrentsHtml = http.get(QStringLiteral("https://ocremix.org/torrents/"));
    if (!torrentsHtml.ok) {
        out << "FAIL torrents page: " << torrentsHtml.error << Qt::endl;
        return 1;
    }
    const auto coverage = HtmlParsers::summarizeTorrentCoverage(
        HtmlParsers::parseTorrentList(QString::fromUtf8(torrentsHtml.body)));
    if (coverage.lastCoveredRemix < 4000 || coverage.collections.size() < 2) {
        out << "FAIL torrent coverage last=" << coverage.lastCoveredRemix
            << " collections=" << coverage.collections.size() << Qt::endl;
        return 1;
    }
    out << "OK torrents cover through OCR" << coverage.lastCoveredRemix
        << " (" << coverage.collectionTrackCount << " tracks, "
        << coverage.albums.size() << " album torrents)" << Qt::endl;
    out << "SMOKE OK" << Qt::endl;
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(65001);
#endif
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--docs-screenshots") == 0) {
            qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty()
                                            ? QByteArray("offscreen")
                                            : qgetenv("QT_QPA_PLATFORM"));
            break;
        }
    }
    QApplication app(argc, argv);
    app.setApplicationName(QString::fromLatin1(AppInfo::name()));
    app.setApplicationVersion(QString::fromLatin1(AppInfo::version()));
    app.setOrganizationName(QString::fromLatin1(AppInfo::organization()));
    app.setDesktopFileName(QString::fromLatin1(AppInfo::id()));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/org.ocrcatalog.OCRCatalog.svg")));

    QFile theme(QStringLiteral(":/theme.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--smoke")))
        return runSmoke();

    const int shot = args.indexOf(QStringLiteral("--docs-screenshots"));
    if (shot >= 0) {
        QStandardPaths::setTestModeEnabled(true);
        for (auto loc : {QStandardPaths::AppDataLocation, QStandardPaths::AppConfigLocation})
            QDir(QStandardPaths::writableLocation(loc)).removeRecursively();
        Settings::instance().setMenuInstallPrompted(true);
        MainWindow window;
        if (!window.ready())
            return 1;
        const QString dir = (shot + 1 < args.size() && !args.at(shot + 1).startsWith(QLatin1Char('-')))
            ? args.at(shot + 1)
            : QStringLiteral("docs/screenshots");
        return window.writeDocsScreenshots(dir);
    }

    qRegisterMetaType<RemixRecord>("RemixRecord");
    qRegisterMetaType<AlbumRecord>("AlbumRecord");
    qRegisterMetaType<DownloadJob>("DownloadJob");
    qRegisterMetaType<QVector<RemixRecord>>("QVector<RemixRecord>");
    qRegisterMetaType<QVector<AlbumRecord>>("QVector<AlbumRecord>");

    MainWindow window;
    if (!window.ready())
        return 1;
    window.show();
    return app.exec();
}
