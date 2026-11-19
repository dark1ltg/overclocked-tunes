#include "CatalogStore.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

namespace {

QString joinList(const QStringList &list)
{
    return list.join(QLatin1Char('\n'));
}

QStringList splitList(const QString &text)
{
    if (text.trimmed().isEmpty())
        return {};
    return text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

} // namespace

CatalogStore::CatalogStore(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("ocr_catalog_main"))
{
}

bool CatalogStore::open()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/catalog.sqlite");

    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(path);
    if (!database.open()) {
        m_lastError = database.lastError().text();
        return false;
    }

    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    const bool ok =
        exec(QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS remixes (
                ocr_id TEXT PRIMARY KEY,
                ocr_number INTEGER NOT NULL,
                title TEXT,
                game TEXT,
                artists TEXT,
                posted TEXT,
                page_url TEXT,
                thumbnail_url TEXT,
                duration TEXT,
                mirrors TEXT,
                status TEXT,
                local_path TEXT,
                last_error TEXT
            )
        )"))
        && exec(QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS albums (
                album_id INTEGER PRIMARY KEY,
                slug TEXT,
                title TEXT,
                catalog TEXT,
                classification TEXT,
                released TEXT,
                page_url TEXT,
                torrent_url TEXT,
                http_homepage TEXT,
                zip_mirrors TEXT,
                ocr_published INTEGER,
                status TEXT,
                local_path TEXT,
                last_error TEXT
            )
        )"))
        && exec(QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS meta (
                key TEXT PRIMARY KEY,
                value TEXT
            )
        )"));
    return ok;
}

bool CatalogStore::exec(const QString &sql)
{
    QSqlQuery q(db());
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QSqlDatabase CatalogStore::db() const
{
    return QSqlDatabase::database(m_connectionName);
}

void CatalogStore::upsertRemixes(const QVector<RemixRecord> &records)
{
    QSqlQuery q(db());
    db().transaction();
    q.prepare(QStringLiteral(R"(
        INSERT INTO remixes (ocr_id, ocr_number, title, game, artists, posted, page_url,
                             thumbnail_url, duration, mirrors, status, local_path, last_error)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(ocr_id) DO UPDATE SET
            ocr_number=excluded.ocr_number,
            title=excluded.title,
            game=excluded.game,
            artists=excluded.artists,
            posted=excluded.posted,
            page_url=excluded.page_url,
            thumbnail_url=excluded.thumbnail_url,
            duration=COALESCE(NULLIF(excluded.duration, ''), remixes.duration),
            mirrors=CASE WHEN excluded.mirrors = '' THEN remixes.mirrors ELSE excluded.mirrors END,
            status=CASE WHEN remixes.status IN ('downloaded', 'unavailable') THEN remixes.status ELSE excluded.status END,
            local_path=CASE WHEN excluded.local_path = '' THEN remixes.local_path ELSE excluded.local_path END
    )"));
    for (const auto &r : records) {
        q.addBindValue(r.ocrId);
        q.addBindValue(r.ocrNumber);
        q.addBindValue(r.title);
        q.addBindValue(r.game);
        q.addBindValue(r.artists);
        q.addBindValue(r.posted);
        q.addBindValue(r.pageUrl);
        q.addBindValue(r.thumbnailUrl);
        q.addBindValue(r.duration);
        q.addBindValue(joinList(r.mirrors));
        q.addBindValue(r.status.isEmpty() ? QStringLiteral("available") : r.status);
        q.addBindValue(r.localPath);
        q.addBindValue(r.lastError);
        q.exec();
        q.finish();
    }
    db().commit();
}

void CatalogStore::updateRemix(const RemixRecord &record)
{
    upsertRemixes({record});
}

RemixRecord CatalogStore::remixFromQuery(const QSqlQuery &q) const
{
    RemixRecord r;
    r.ocrId = q.value(0).toString();
    r.ocrNumber = q.value(1).toInt();
    r.title = q.value(2).toString();
    r.game = q.value(3).toString();
    r.artists = q.value(4).toString();
    r.posted = q.value(5).toString();
    r.pageUrl = q.value(6).toString();
    r.thumbnailUrl = q.value(7).toString();
    r.duration = q.value(8).toString();
    r.mirrors = splitList(q.value(9).toString());
    r.status = q.value(10).toString();
    r.localPath = q.value(11).toString();
    r.lastError = q.value(12).toString();
    return r;
}

AlbumRecord CatalogStore::albumFromQuery(const QSqlQuery &q) const
{
    AlbumRecord a;
    a.albumId = q.value(0).toInt();
    a.slug = q.value(1).toString();
    a.title = q.value(2).toString();
    a.catalog = q.value(3).toString();
    a.classification = q.value(4).toString();
    a.released = q.value(5).toString();
    a.pageUrl = q.value(6).toString();
    a.torrentUrl = q.value(7).toString();
    a.httpHomepage = q.value(8).toString();
    a.zipMirrors = splitList(q.value(9).toString());
    a.ocrPublished = q.value(10).toInt() != 0;
    a.status = q.value(11).toString();
    a.localPath = q.value(12).toString();
    a.lastError = q.value(13).toString();
    return a;
}

RemixRecord CatalogStore::remix(const QString &ocrId) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM remixes WHERE ocr_id=?"));
    q.addBindValue(ocrId);
    if (q.exec() && q.next())
        return remixFromQuery(q);
    return {};
}

QVector<RemixRecord> CatalogStore::remixes(const QString &search) const
{
    QVector<RemixRecord> out;
    QSqlQuery q(db());
    if (search.trimmed().isEmpty()) {
        q.exec(QStringLiteral("SELECT * FROM remixes ORDER BY ocr_number DESC"));
    } else {
        const QString like = QLatin1Char('%') + search.trimmed() + QLatin1Char('%');
        q.prepare(QStringLiteral(R"(
            SELECT * FROM remixes
            WHERE ocr_id LIKE ? OR title LIKE ? OR game LIKE ? OR artists LIKE ?
            ORDER BY ocr_number DESC
        )"));
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(like);
        q.exec();
    }
    while (q.next())
        out.push_back(remixFromQuery(q));
    return out;
}

QVector<RemixRecord> CatalogStore::remixesNeedingDownload() const
{
    QVector<RemixRecord> out;
    QSqlQuery q(db());
    q.exec(QStringLiteral("SELECT * FROM remixes WHERE status NOT IN ('downloaded', 'unavailable') ORDER BY ocr_number"));
    while (q.next())
        out.push_back(remixFromQuery(q));
    return out;
}

int CatalogStore::remixCount() const
{
    QSqlQuery q(db());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM remixes"));
    return q.next() ? q.value(0).toInt() : 0;
}

int CatalogStore::remixDownloadedCount() const
{
    QSqlQuery q(db());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM remixes WHERE status='downloaded'"));
    return q.next() ? q.value(0).toInt() : 0;
}

void CatalogStore::upsertAlbums(const QVector<AlbumRecord> &records)
{
    QSqlQuery q(db());
    db().transaction();
    q.prepare(QStringLiteral(R"(
        INSERT INTO albums (album_id, slug, title, catalog, classification, released, page_url,
                            torrent_url, http_homepage, zip_mirrors, ocr_published, status, local_path, last_error)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(album_id) DO UPDATE SET
            slug=excluded.slug,
            title=excluded.title,
            catalog=excluded.catalog,
            classification=excluded.classification,
            released=excluded.released,
            page_url=excluded.page_url,
            torrent_url=CASE WHEN excluded.torrent_url = '' THEN albums.torrent_url ELSE excluded.torrent_url END,
            http_homepage=CASE WHEN excluded.http_homepage = '' THEN albums.http_homepage ELSE excluded.http_homepage END,
            zip_mirrors=CASE WHEN excluded.zip_mirrors = '' THEN albums.zip_mirrors ELSE excluded.zip_mirrors END,
            ocr_published=excluded.ocr_published,
            status=CASE WHEN albums.status IN ('downloaded', 'catalog-only') THEN albums.status ELSE excluded.status END,
            local_path=CASE WHEN excluded.local_path = '' THEN albums.local_path ELSE excluded.local_path END
    )"));
    for (const auto &a : records) {
        q.addBindValue(a.albumId);
        q.addBindValue(a.slug);
        q.addBindValue(a.title);
        q.addBindValue(a.catalog);
        q.addBindValue(a.classification);
        q.addBindValue(a.released);
        q.addBindValue(a.pageUrl);
        q.addBindValue(a.torrentUrl);
        q.addBindValue(a.httpHomepage);
        q.addBindValue(joinList(a.zipMirrors));
        q.addBindValue(a.ocrPublished ? 1 : 0);
        q.addBindValue(a.status.isEmpty() ? QStringLiteral("available") : a.status);
        q.addBindValue(a.localPath);
        q.addBindValue(a.lastError);
        q.exec();
        q.finish();
    }
    db().commit();
}

void CatalogStore::updateAlbum(const AlbumRecord &record)
{
    upsertAlbums({record});
}

AlbumRecord CatalogStore::album(int albumId) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM albums WHERE album_id=?"));
    q.addBindValue(albumId);
    if (q.exec() && q.next())
        return albumFromQuery(q);
    return {};
}

QVector<AlbumRecord> CatalogStore::albums(const QString &search) const
{
    QVector<AlbumRecord> out;
    QSqlQuery q(db());
    if (search.trimmed().isEmpty()) {
        q.exec(QStringLiteral("SELECT * FROM albums ORDER BY album_id DESC"));
    } else {
        const QString like = QLatin1Char('%') + search.trimmed() + QLatin1Char('%');
        q.prepare(QStringLiteral(R"(
            SELECT * FROM albums
            WHERE title LIKE ? OR catalog LIKE ? OR slug LIKE ?
            ORDER BY album_id DESC
        )"));
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(like);
        q.exec();
    }
    while (q.next())
        out.push_back(albumFromQuery(q));
    return out;
}

QVector<AlbumRecord> CatalogStore::albumsNeedingDownload() const
{
    QVector<AlbumRecord> out;
    QSqlQuery q(db());
    q.exec(QStringLiteral(
        "SELECT * FROM albums WHERE ocr_published=1 AND status NOT IN ('downloaded', 'catalog-only') ORDER BY album_id"));
    while (q.next())
        out.push_back(albumFromQuery(q));
    return out;
}

int CatalogStore::albumCount() const
{
    QSqlQuery q(db());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM albums"));
    return q.next() ? q.value(0).toInt() : 0;
}

int CatalogStore::albumDownloadedCount() const
{
    QSqlQuery q(db());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE status='downloaded'"));
    return q.next() ? q.value(0).toInt() : 0;
}

void CatalogStore::setMeta(const QString &key, const QString &value)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

QString CatalogStore::meta(const QString &key) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key=?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

void CatalogStore::markExistingFiles(const QString &libraryPath)
{
    if (libraryPath.isEmpty())
        return;

    const QDir remixDir(libraryPath + QStringLiteral("/remixes"));
    const QDir albumDir(libraryPath + QStringLiteral("/albums"));
    const QDir torrentDir(libraryPath + QStringLiteral("/torrents"));

    auto remixesAll = remixes();
    for (auto &r : remixesAll) {
        if (!r.localPath.isEmpty() && QFileInfo::exists(r.localPath)) {
            r.status = QStringLiteral("downloaded");
            updateRemix(r);
            continue;
        }
        for (const auto &mirror : r.mirrors) {
            const QString name = QFileInfo(QUrl(mirror).path()).fileName();
            const QString candidate = remixDir.filePath(name);
            if (QFileInfo::exists(candidate)) {
                r.localPath = candidate;
                r.status = QStringLiteral("downloaded");
                updateRemix(r);
                break;
            }
        }
    }

    auto albumsAll = albums();
    for (auto &a : albumsAll) {
        if (!a.localPath.isEmpty() && QFileInfo::exists(a.localPath)) {
            a.status = QStringLiteral("downloaded");
            updateAlbum(a);
            continue;
        }
        const QString zipName = a.catalog.isEmpty() ? a.slug + QStringLiteral(".zip") : a.catalog + QStringLiteral(".zip");
        const QString zipPath = albumDir.filePath(zipName);
        if (QFileInfo::exists(zipPath)) {
            a.localPath = zipPath;
            a.status = QStringLiteral("downloaded");
            updateAlbum(a);
            continue;
        }
        for (const auto &mirror : a.zipMirrors) {
            const QString name = QFileInfo(QUrl(mirror).path()).fileName();
            const QString candidate = albumDir.filePath(name);
            if (QFileInfo::exists(candidate)) {
                a.localPath = candidate;
                a.status = QStringLiteral("downloaded");
                updateAlbum(a);
                break;
            }
        }
        Q_UNUSED(torrentDir);
    }
}
