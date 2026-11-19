#include "DownloadQueue.h"

#include "HtmlParsers.h"
#include "HttpClient.h"
#include "OfficialTorrentEngine.h"
#include "appinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QtGlobal>
#include <QUrl>

DownloadQueue::DownloadQueue(QObject *parent)
    : QObject(parent)
{
}

void DownloadQueue::enqueueRemixes(const QVector<RemixRecord> &records)
{
    m_remixes.append(records);
}

void DownloadQueue::enqueueAlbums(const QVector<AlbumRecord> &records)
{
    m_albums.append(records);
}

QString DownloadQueue::filenameFromUrl(const QString &url) const
{
    const QString path = QUrl(url).path();
    return QUrl::fromPercentEncoding(QFileInfo(path).fileName().toUtf8());
}

QString DownloadQueue::torrentDir() const
{
    return m_libraryPath + QStringLiteral("/torrents");
}

QString DownloadQueue::collectionDir() const
{
    return m_libraryPath + QStringLiteral("/collections");
}

void DownloadQueue::maybeStartInAppTorrent(const QString &sourceUrl, const QString &torrentFile)
{
    if (!m_downloadInApp || !m_engine || !OfficialTorrentEngine::available())
        return;
    QDir().mkpath(collectionDir());
    m_engine->addOfficialTorrent(sourceUrl, torrentFile, collectionDir());
}

void DownloadQueue::start()
{
    m_cancel = false;
    m_running = true;

    if (m_preferTorrents) {
        emit queueProgress(0, 0, tr("Starting with official collection torrents OCR 1–4000 and 4001–4473…"));
        if (!applyOfficialTorrents() && m_cancel) {
            m_remixes.clear();
            m_albums.clear();
            m_running = false;
            emit finished(false, tr("Downloads cancelled."));
            return;
        }
    }

    const int total = m_remixes.size() + m_albums.size();
    int done = 0;
    if (total == 0) {
        m_running = false;
        emit finished(true, tr("Nothing left after official torrents. Remaining items are already covered or on disk."));
        return;
    }

    for (auto record : m_remixes) {
        if (m_cancel)
            break;
        emit queueProgress(done, total, tr("Downloading %1").arg(record.ocrId));
        downloadRemix(record);
        ++done;
        if (!m_cancel)
            QThread::msleep(static_cast<unsigned long>(m_delayMs));
    }
    for (auto record : m_albums) {
        if (m_cancel)
            break;
        emit queueProgress(done, total, tr("Downloading album %1").arg(record.title));
        downloadAlbum(record);
        ++done;
        if (!m_cancel)
            QThread::msleep(static_cast<unsigned long>(m_delayMs));
    }

    m_remixes.clear();
    m_albums.clear();
    m_running = false;
    emit finished(!m_cancel, m_cancel ? tr("Downloads cancelled.") : tr("Downloads finished."));
}

bool DownloadQueue::applyOfficialTorrents()
{
    HttpClient http;
    QVector<TorrentListing> collections = HtmlParsers::startingCollectionTorrents();
    QVector<TorrentListing> listings;

    const QString pageUrl = QStringLiteral("%1/torrents/").arg(QLatin1String(AppInfo::siteRoot()));
    const auto page = http.get(pageUrl);
    if (page.ok) {
        listings = HtmlParsers::parseTorrentList(QString::fromUtf8(page.body));
        int coveredEnd = 0;
        for (const auto &starter : collections)
            coveredEnd = qMax(coveredEnd, starter.rangeEnd);
        for (const auto &extra : HtmlParsers::summarizeTorrentCoverage(listings).collections) {
            if (extra.rangeStart <= coveredEnd)
                continue;
            bool seen = false;
            for (const auto &existing : collections) {
                if (existing.url == extra.url) {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                collections.push_back(extra);
        }
    } else {
        emit queueProgress(0, 0,
                           tr("Could not read torrents page (%1); still using the two official collection torrents.")
                               .arg(page.error));
    }

    QDir().mkpath(torrentDir());
    QStringList saved;

    int step = 0;
    const int torrentJobs = qMax(collections.size(), 1);
    struct Range { int from = 0; int to = 0; };
    QVector<Range> covered;
    for (const auto &collection : collections) {
        if (m_cancel)
            return false;
        DownloadJob job;
        job.kind = DownloadJob::CollectionTorrent;
        job.itemId = QStringLiteral("collection-%1-%2").arg(collection.rangeStart).arg(collection.rangeEnd);
        job.displayName = collection.name;
        job.status = QStringLiteral("running");
        emit jobUpdated(job);
        emit queueProgress(++step, torrentJobs, tr("Saving collection torrent %1").arg(collection.name));

        const QString dest = torrentDir() + QLatin1Char('/') + filenameFromUrl(collection.url);
        const bool existed = QFileInfo::exists(dest) && QFileInfo(dest).size() > 0;
        bool ok = existed;
        if (!ok) {
            const auto result = http.downloadToFile(collection.url, dest);
            ok = result.ok;
            if (!ok)
                job.error = result.error;
        }
        if (ok) {
            if (!existed)
                saved << dest;
            job.status = QStringLiteral("done");
            if (collection.rangeEnd >= collection.rangeStart && collection.rangeEnd > 0)
                covered.push_back({collection.rangeStart, collection.rangeEnd});
            const QString note = tr("Covered by official collection torrent: %1").arg(collection.name);
            emit remixRangeCovered(collection.rangeStart, collection.rangeEnd, note);
            maybeStartInAppTorrent(collection.url, dest);
        } else {
            job.status = QStringLiteral("error");
        }
        emit jobUpdated(job);
        QThread::msleep(static_cast<unsigned long>(m_delayMs));
    }

    QVector<RemixRecord> remainingRemixes;
    remainingRemixes.reserve(m_remixes.size());
    for (const auto &r : m_remixes) {
        bool inRange = false;
        for (const auto &range : covered) {
            if (r.ocrNumber >= range.from && r.ocrNumber <= range.to) {
                inRange = true;
                break;
            }
        }
        if (!inRange)
            remainingRemixes.push_back(r);
    }
    m_remixes = remainingRemixes;

    if (m_includeAlbums) {
        for (auto &album : m_albums) {
            if (album.torrentUrl.isEmpty()) {
                if (const auto *match = HtmlParsers::matchAlbumTorrent(album, listings))
                    album.torrentUrl = match->url;
            } else {
                album.torrentUrl = HtmlParsers::preferHttps(album.torrentUrl);
            }
        }
    }

    int lastCovered = 0;
    int trackCount = 0;
    for (const auto &range : covered) {
        lastCovered = qMax(lastCovered, range.to);
        if (range.to >= range.from)
            trackCount += range.to - range.from + 1;
    }

    emit torrentsReady(saved);
    emit coverageSummary(lastCovered, trackCount, m_remixes.size(), m_albums.size());
    return true;
}

bool DownloadQueue::downloadRemix(RemixRecord record)
{
    DownloadJob job;
    job.kind = DownloadJob::Remix;
    job.itemId = record.ocrId;
    job.displayName = record.ocrId + QStringLiteral(" \u2014 ") + record.title;
    job.status = QStringLiteral("running");
    emit jobUpdated(job);

    HttpClient http;
    if (record.mirrors.isEmpty()) {
        const auto page = http.get(record.pageUrl);
        if (page.status == 404) {
            record.status = QStringLiteral("unavailable");
            record.lastError = QStringLiteral("Removed from OC ReMix");
            job.status = QStringLiteral("skipped");
            job.error = record.lastError;
            emit remixUpdated(record);
            emit jobUpdated(job);
            return false;
        }
        if (!page.ok) {
            record.status = QStringLiteral("error");
            record.lastError = page.error;
            job.status = QStringLiteral("error");
            job.error = page.error;
            emit remixUpdated(record);
            emit jobUpdated(job);
            return false;
        }
        record = HtmlParsers::parseRemixDetail(QString::fromUtf8(page.body), record);
        emit remixUpdated(record);
    }

    if (record.mirrors.isEmpty()) {
        record.status = QStringLiteral("unavailable");
        record.lastError = QStringLiteral("No download mirrors");
        job.status = QStringLiteral("error");
        job.error = record.lastError;
        emit remixUpdated(record);
        emit jobUpdated(job);
        return false;
    }

    const QString destDir = m_libraryPath + QStringLiteral("/remixes");
    QDir().mkpath(destDir);
    const QString fileName = filenameFromUrl(record.mirrors.first());
    const QString dest = destDir + QLatin1Char('/') + fileName;
    if (QFileInfo::exists(dest) && QFileInfo(dest).size() > 0) {
        record.localPath = dest;
        record.status = QStringLiteral("downloaded");
        job.status = QStringLiteral("skipped");
        emit remixUpdated(record);
        emit jobUpdated(job);
        return true;
    }

    QString lastError;
    for (const auto &mirror : HtmlParsers::preferMirrors(record.mirrors)) {
        if (m_cancel)
            break;
        job.displayName = fileName;
        emit jobUpdated(job);
        const auto result = http.downloadToFile(mirror, dest, 0, [&](qint64 done, qint64 total) {
            job.bytesDone = done;
            job.bytesTotal = total;
            emit jobUpdated(job);
        });
        if (result.ok) {
            record.localPath = dest;
            record.status = QStringLiteral("downloaded");
            record.lastError.clear();
            job.status = QStringLiteral("done");
            emit remixUpdated(record);
            emit jobUpdated(job);
            return true;
        }
        lastError = result.error + QStringLiteral(" (") + mirror + QLatin1Char(')');
        QFile::remove(dest);
    }

    record.status = QStringLiteral("error");
    record.lastError = lastError;
    job.status = QStringLiteral("error");
    job.error = lastError;
    emit remixUpdated(record);
    emit jobUpdated(job);
    return false;
}

bool DownloadQueue::downloadAlbum(AlbumRecord record)
{
    DownloadJob job;
    job.kind = DownloadJob::AlbumZip;
    job.itemId = QString::number(record.albumId);
    job.displayName = record.title;
    job.status = QStringLiteral("running");
    emit jobUpdated(job);

    if (!record.ocrPublished) {
        record.status = QStringLiteral("catalog-only");
        job.status = QStringLiteral("skipped");
        job.error = tr("Not an OC ReMix published album");
        emit albumUpdated(record);
        emit jobUpdated(job);
        return false;
    }

    HttpClient http;
    if (record.torrentUrl.isEmpty() || record.httpHomepage.isEmpty()) {
        const auto page = http.get(record.pageUrl);
        if (page.ok) {
            HtmlParsers::parseAlbumDetail(&record, QString::fromUtf8(page.body));
            record.torrentUrl = HtmlParsers::preferHttps(record.torrentUrl);
            emit albumUpdated(record);
        }
    }
    if (record.zipMirrors.isEmpty() && !record.httpHomepage.isEmpty() && !m_preferTorrents) {
        const auto home = http.get(record.httpHomepage);
        if (home.ok)
            record.zipMirrors = HtmlParsers::parseAlbumZipMirrors(QString::fromUtf8(home.body));
        emit albumUpdated(record);
    }

    const QString albumDir = m_libraryPath + QStringLiteral("/albums");
    QDir().mkpath(albumDir);
    QDir().mkpath(torrentDir());

    auto saveTorrent = [&]() -> bool {
        if (record.torrentUrl.isEmpty())
            return false;
        const QString torrentName = filenameFromUrl(record.torrentUrl);
        const QString dest = torrentDir() + QLatin1Char('/') + torrentName;
        bool ok = QFileInfo::exists(dest) && QFileInfo(dest).size() > 0;
        if (!ok) {
            const auto result = http.downloadToFile(record.torrentUrl, dest);
            ok = result.ok;
            if (!ok)
                record.lastError = result.error;
        }
        if (!ok)
            return false;
        record.localPath = dest;
        record.status = QStringLiteral("in-torrent");
        if (m_downloadInApp && OfficialTorrentEngine::available()) {
            record.lastError = tr("Downloading official album torrent inside OCR Catalog.");
            maybeStartInAppTorrent(record.torrentUrl, dest);
        } else {
            record.lastError = tr("Saved official album torrent. Open it in a BitTorrent client "
                                  "(qBittorrent, Transmission, Deluge).");
        }
        job.kind = DownloadJob::Torrent;
        job.status = QStringLiteral("done");
        job.displayName = torrentName;
        emit albumUpdated(record);
        emit jobUpdated(job);
        return true;
    };

    if (m_preferTorrents && saveTorrent())
        return true;

    if (record.zipMirrors.isEmpty() && !record.httpHomepage.isEmpty()) {
        const auto home = http.get(record.httpHomepage);
        if (home.ok)
            record.zipMirrors = HtmlParsers::parseAlbumZipMirrors(QString::fromUtf8(home.body));
        emit albumUpdated(record);
    }

    if (!record.zipMirrors.isEmpty()) {
        const QString fileName = filenameFromUrl(record.zipMirrors.first());
        const QString dest = albumDir + QLatin1Char('/') + fileName;
        if (QFileInfo::exists(dest) && QFileInfo(dest).size() > 0) {
            record.localPath = dest;
            record.status = QStringLiteral("downloaded");
            job.status = QStringLiteral("skipped");
            emit albumUpdated(record);
            emit jobUpdated(job);
            return true;
        }
        QString lastError;
        for (const auto &mirror : HtmlParsers::preferMirrors(record.zipMirrors)) {
            if (m_cancel)
                break;
            const auto result = http.downloadToFile(mirror, dest, 0, [&](qint64 done, qint64 total) {
                job.bytesDone = done;
                job.bytesTotal = total;
                emit jobUpdated(job);
            });
            if (result.ok) {
                record.localPath = dest;
                record.status = QStringLiteral("downloaded");
                record.lastError.clear();
                job.status = QStringLiteral("done");
                emit albumUpdated(record);
                emit jobUpdated(job);
                return true;
            }
            lastError = result.error;
            QFile::remove(dest);
        }
        record.lastError = lastError;
    }

    if (saveTorrent())
        return true;

    if (record.status != QStringLiteral("downloaded"))
        record.status = QStringLiteral("error");
    job.status = QStringLiteral("error");
    job.error = record.lastError;
    emit albumUpdated(record);
    emit jobUpdated(job);
    return false;
}
