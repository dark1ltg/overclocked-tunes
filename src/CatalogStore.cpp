#include "CatalogStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
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

int ocrNumberFromFileName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("OCR(\\d{5,})"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

} // namespace

CatalogStore::CatalogStore(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("ocr_catalog_main"))
{
}

QString CatalogStore::escapeLike(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

bool CatalogStore::open()
{
    if (m_open) {
        const QSqlDatabase existing = db();
        if (existing.isValid() && existing.isOpen())
            return true;
        m_open = false;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_lastError = tr("The Qt SQLite plugin is missing. Install the Qt SQL sqlite driver "
                         "(for example libqt6sql6-sqlite) and rebuild the package with that plugin bundled.");
        return false;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dir)) {
        m_lastError = tr("Cannot create catalog folder %1").arg(dir);
        return false;
    }
    const QString path = dir + QStringLiteral("/catalog.sqlite");

    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(path);
    if (!database.open()) {
        m_lastError = database.lastError().text();
        if (m_lastError.isEmpty())
            m_lastError = tr("Could not open %1").arg(path);
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
    m_open = ok;
    if (!ok && m_lastError.isEmpty())
        m_lastError = tr("Could not initialize the catalog database.");
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
    if (records.isEmpty())
        return;
    QSqlQuery q(db());
    if (!db().transaction())
        return;
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
            status=CASE WHEN remixes.status IN ('downloaded', 'unavailable', 'in-torrent') THEN remixes.status ELSE excluded.status END,
            local_path=CASE WHEN excluded.local_path = '' THEN remixes.local_path ELSE excluded.local_path END
    )"));
    bool ok = true;
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
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            ok = false;
            break;
        }
        q.finish();
    }
    if (ok)
        db().commit();
    else
        db().rollback();
}

bool CatalogStore::updateRemix(const RemixRecord &record)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(R"(
        INSERT INTO remixes (ocr_id, ocr_number, title, game, artists, posted, page_url,
                             thumbnail_url, duration, mirrors, status, local_path, last_error)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(ocr_id) DO UPDATE SET
            ocr_number=excluded.ocr_number,
            title=CASE WHEN excluded.title = '' THEN remixes.title ELSE excluded.title END,
            game=CASE WHEN excluded.game = '' THEN remixes.game ELSE excluded.game END,
            artists=CASE WHEN excluded.artists = '' THEN remixes.artists ELSE excluded.artists END,
            posted=CASE WHEN excluded.posted = '' THEN remixes.posted ELSE excluded.posted END,
            page_url=CASE WHEN excluded.page_url = '' THEN remixes.page_url ELSE excluded.page_url END,
            thumbnail_url=CASE WHEN excluded.thumbnail_url = '' THEN remixes.thumbnail_url ELSE excluded.thumbnail_url END,
            duration=CASE WHEN excluded.duration = '' THEN remixes.duration ELSE excluded.duration END,
            mirrors=CASE WHEN excluded.mirrors = '' THEN remixes.mirrors ELSE excluded.mirrors END,
            status=excluded.status,
            local_path=CASE WHEN excluded.local_path = '' THEN remixes.local_path ELSE excluded.local_path END,
            last_error=excluded.last_error
    )"));
    q.addBindValue(record.ocrId);
    q.addBindValue(record.ocrNumber);
    q.addBindValue(record.title);
    q.addBindValue(record.game);
    q.addBindValue(record.artists);
    q.addBindValue(record.posted);
    q.addBindValue(record.pageUrl);
    q.addBindValue(record.thumbnailUrl);
    q.addBindValue(record.duration);
    q.addBindValue(joinList(record.mirrors));
    q.addBindValue(record.status.isEmpty() ? QStringLiteral("available") : record.status);
    q.addBindValue(record.localPath);
    q.addBindValue(record.lastError);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
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
    q.prepare(QStringLiteral(
        "SELECT ocr_id, ocr_number, title, game, artists, posted, page_url, thumbnail_url, "
        "duration, mirrors, status, local_path, last_error FROM remixes WHERE ocr_id=?"));
    q.addBindValue(ocrId);
    if (q.exec() && q.next())
        return remixFromQuery(q);
    return {};
}

QVector<RemixRecord> CatalogStore::remixes(const QString &search) const
{
    QVector<RemixRecord> out;
    QSqlQuery q(db());
    const QString columns = QStringLiteral(
        "SELECT ocr_id, ocr_number, title, game, artists, posted, page_url, thumbnail_url, "
        "duration, mirrors, status, local_path, last_error FROM remixes");
    if (search.trimmed().isEmpty()) {
        q.exec(columns + QStringLiteral(" ORDER BY ocr_number DESC"));
    } else {
        const QString like = QLatin1Char('%') + escapeLike(search.trimmed()) + QLatin1Char('%');
        q.prepare(columns + QStringLiteral(R"(
            WHERE ocr_id LIKE ? ESCAPE '\' OR title LIKE ? ESCAPE '\'
               OR game LIKE ? ESCAPE '\' OR artists LIKE ? ESCAPE '\'
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
    q.exec(QStringLiteral(
        "SELECT ocr_id, ocr_number, title, game, artists, posted, page_url, thumbnail_url, "
        "duration, mirrors, status, local_path, last_error FROM remixes "
        "WHERE status NOT IN ('downloaded', 'unavailable', 'in-torrent') ORDER BY ocr_number"));
    while (q.next())
        out.push_back(remixFromQuery(q));
    return out;
}

int CatalogStore::countQuery(const QString &sql) const
{
    QSqlQuery q(db());
    q.exec(sql);
    return q.next() ? q.value(0).toInt() : 0;
}

int CatalogStore::remixCount() const
{
    return countQuery(QStringLiteral("SELECT COUNT(*) FROM remixes"));
}

int CatalogStore::remixDownloadedCount() const
{
    return countQuery(QStringLiteral("SELECT COUNT(*) FROM remixes WHERE status='downloaded'"));
}

int CatalogStore::remixTorrentCoveredCount() const
{
    return countQuery(QStringLiteral("SELECT COUNT(*) FROM remixes WHERE status='in-torrent'"));
}

int CatalogStore::remixNeedingCount() const
{
    return countQuery(QStringLiteral(
        "SELECT COUNT(*) FROM remixes WHERE status NOT IN ('downloaded', 'unavailable', 'in-torrent')"));
}

void CatalogStore::markRemixesInTorrentRange(int from, int to, const QString &note)
{
    if (to < from)
        return;
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "UPDATE remixes SET status='in-torrent', last_error=? "
        "WHERE ocr_number BETWEEN ? AND ? AND status NOT IN ('downloaded', 'unavailable')"));
    q.addBindValue(note);
    q.addBindValue(from);
    q.addBindValue(to);
    if (!q.exec())
        m_lastError = q.lastError().text();
}

void CatalogStore::upsertAlbums(const QVector<AlbumRecord> &records)
{
    if (records.isEmpty())
        return;
    QSqlQuery q(db());
    if (!db().transaction())
        return;
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
            status=CASE WHEN albums.status IN ('downloaded', 'catalog-only', 'in-torrent') THEN albums.status ELSE excluded.status END,
            local_path=CASE WHEN excluded.local_path = '' THEN albums.local_path ELSE excluded.local_path END
    )"));
    bool ok = true;
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
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            ok = false;
            break;
        }
        q.finish();
    }
    if (ok)
        db().commit();
    else
        db().rollback();
}

bool CatalogStore::updateAlbum(const AlbumRecord &record)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(R"(
        INSERT INTO albums (album_id, slug, title, catalog, classification, released, page_url,
                            torrent_url, http_homepage, zip_mirrors, ocr_published, status, local_path, last_error)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(album_id) DO UPDATE SET
            slug=CASE WHEN excluded.slug = '' THEN albums.slug ELSE excluded.slug END,
            title=CASE WHEN excluded.title = '' THEN albums.title ELSE excluded.title END,
            catalog=CASE WHEN excluded.catalog = '' THEN albums.catalog ELSE excluded.catalog END,
            classification=CASE WHEN excluded.classification = '' THEN albums.classification ELSE excluded.classification END,
            released=CASE WHEN excluded.released = '' THEN albums.released ELSE excluded.released END,
            page_url=CASE WHEN excluded.page_url = '' THEN albums.page_url ELSE excluded.page_url END,
            torrent_url=CASE WHEN excluded.torrent_url = '' THEN albums.torrent_url ELSE excluded.torrent_url END,
            http_homepage=CASE WHEN excluded.http_homepage = '' THEN albums.http_homepage ELSE excluded.http_homepage END,
            zip_mirrors=CASE WHEN excluded.zip_mirrors = '' THEN albums.zip_mirrors ELSE excluded.zip_mirrors END,
            ocr_published=excluded.ocr_published,
            status=excluded.status,
            local_path=CASE WHEN excluded.local_path = '' THEN albums.local_path ELSE excluded.local_path END,
            last_error=excluded.last_error
    )"));
    q.addBindValue(record.albumId);
    q.addBindValue(record.slug);
    q.addBindValue(record.title);
    q.addBindValue(record.catalog);
    q.addBindValue(record.classification);
    q.addBindValue(record.released);
    q.addBindValue(record.pageUrl);
    q.addBindValue(record.torrentUrl);
    q.addBindValue(record.httpHomepage);
    q.addBindValue(joinList(record.zipMirrors));
    q.addBindValue(record.ocrPublished ? 1 : 0);
    q.addBindValue(record.status.isEmpty() ? QStringLiteral("available") : record.status);
    q.addBindValue(record.localPath);
    q.addBindValue(record.lastError);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

AlbumRecord CatalogStore::album(int albumId) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT album_id, slug, title, catalog, classification, released, page_url, torrent_url, "
        "http_homepage, zip_mirrors, ocr_published, status, local_path, last_error "
        "FROM albums WHERE album_id=?"));
    q.addBindValue(albumId);
    if (q.exec() && q.next())
        return albumFromQuery(q);
    return {};
}

QVector<AlbumRecord> CatalogStore::albums(const QString &search) const
{
    QVector<AlbumRecord> out;
    QSqlQuery q(db());
    const QString columns = QStringLiteral(
        "SELECT album_id, slug, title, catalog, classification, released, page_url, torrent_url, "
        "http_homepage, zip_mirrors, ocr_published, status, local_path, last_error FROM albums");
    if (search.trimmed().isEmpty()) {
        q.exec(columns + QStringLiteral(" ORDER BY album_id DESC"));
    } else {
        const QString like = QLatin1Char('%') + escapeLike(search.trimmed()) + QLatin1Char('%');
        q.prepare(columns + QStringLiteral(R"(
            WHERE title LIKE ? ESCAPE '\' OR catalog LIKE ? ESCAPE '\' OR slug LIKE ? ESCAPE '\'
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
        "SELECT album_id, slug, title, catalog, classification, released, page_url, torrent_url, "
        "http_homepage, zip_mirrors, ocr_published, status, local_path, last_error FROM albums "
        "WHERE ocr_published=1 AND status NOT IN ('downloaded', 'catalog-only', 'in-torrent') "
        "ORDER BY album_id"));
    while (q.next())
        out.push_back(albumFromQuery(q));
    return out;
}

int CatalogStore::albumCount() const
{
    return countQuery(QStringLiteral("SELECT COUNT(*) FROM albums"));
}

int CatalogStore::albumDownloadedCount() const
{
    return countQuery(QStringLiteral("SELECT COUNT(*) FROM albums WHERE status='downloaded'"));
}

int CatalogStore::albumTorrentCoveredCount() const
{
    return countQuery(QStringLiteral("SELECT COUNT(*) FROM albums WHERE status='in-torrent'"));
}

int CatalogStore::albumNeedingCount() const
{
    return countQuery(QStringLiteral(
        "SELECT COUNT(*) FROM albums WHERE ocr_published=1 AND status NOT IN ('downloaded', 'catalog-only', 'in-torrent')"));
}

void CatalogStore::setMeta(const QString &key, const QString &value)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    if (!q.exec())
        m_lastError = q.lastError().text();
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
    if (libraryPath.isEmpty() || !m_open)
        return;

    const QDir remixDir(libraryPath + QStringLiteral("/remixes"));
    const QDir albumDir(libraryPath + QStringLiteral("/albums"));
    const QDir collectionDir(libraryPath + QStringLiteral("/collections"));

    if (!db().transaction())
        return;

    auto remixesAll = remixes();
    QHash<int, int> remixIndex;
    remixIndex.reserve(remixesAll.size());
    for (int i = 0; i < remixesAll.size(); ++i)
        remixIndex.insert(remixesAll[i].ocrNumber, i);

    auto markRemixPath = [&](RemixRecord &r, const QString &path) {
        r.localPath = path;
        r.status = path.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive)
            ? QStringLiteral("in-torrent")
            : QStringLiteral("downloaded");
        r.lastError.clear();
        updateRemix(r);
    };

    for (auto &r : remixesAll) {
        if (!r.localPath.isEmpty() && QFileInfo::exists(r.localPath)) {
            const QString status = r.localPath.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive)
                ? QStringLiteral("in-torrent")
                : QStringLiteral("downloaded");
            if (r.status != status) {
                r.status = status;
                updateRemix(r);
            }
            continue;
        }
        for (const auto &mirror : r.mirrors) {
            const QString name = QFileInfo(QUrl(mirror).path()).fileName();
            if (name.isEmpty())
                continue;
            const QString candidate = remixDir.filePath(name);
            if (QFileInfo::exists(candidate)) {
                markRemixPath(r, candidate);
                break;
            }
        }
    }

    if (collectionDir.exists()) {
        QDirIterator it(collectionDir.absolutePath(),
                        QStringList{QStringLiteral("*.mp3"), QStringLiteral("*.ogg"),
                                    QStringLiteral("*.flac"), QStringLiteral("*.m4a")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const int number = ocrNumberFromFileName(QFileInfo(path).fileName());
            if (number <= 0 || !remixIndex.contains(number))
                continue;
            auto &r = remixesAll[remixIndex.value(number)];
            if (r.status == QLatin1String("downloaded") && QFileInfo::exists(r.localPath))
                continue;
            markRemixPath(r, path);
        }
    }

    auto albumsAll = albums();
    for (auto &a : albumsAll) {
        if (!a.localPath.isEmpty() && QFileInfo::exists(a.localPath)) {
            const QString status = a.localPath.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive)
                ? QStringLiteral("in-torrent")
                : QStringLiteral("downloaded");
            if (a.status != status) {
                a.status = status;
                updateAlbum(a);
            }
            continue;
        }
        const QString zipName = a.catalog.isEmpty() ? a.slug + QStringLiteral(".zip") : a.catalog + QStringLiteral(".zip");
        const QString zipPath = albumDir.filePath(zipName);
        if (QFileInfo::exists(zipPath)) {
            a.localPath = zipPath;
            a.status = QStringLiteral("downloaded");
            a.lastError.clear();
            updateAlbum(a);
            continue;
        }
        for (const auto &mirror : a.zipMirrors) {
            const QString name = QFileInfo(QUrl(mirror).path()).fileName();
            if (name.isEmpty())
                continue;
            const QString candidate = albumDir.filePath(name);
            if (QFileInfo::exists(candidate)) {
                a.localPath = candidate;
                a.status = QStringLiteral("downloaded");
                a.lastError.clear();
                updateAlbum(a);
                break;
            }
        }
    }

    db().commit();
}
