#include "DownloadQueue.h"

#include "HtmlParsers.h"
#include "HttpClient.h"
#include "OfficialTorrentEngine.h"
#include "appinfo.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <QtGlobal>
#include <QUrl>
#include <algorithm>

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
    const QUrl parsed(url);
    QString name = parsed.fileName();
    if (name.isEmpty())
        name = QFileInfo(parsed.path()).fileName();
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return {};
    return name;
}

QString DownloadQueue::destForUrl(const QString &dir, const QString &url) const
{
    const QString name = filenameFromUrl(url);
    if (name.isEmpty())
        return {};
    const QString dest = QDir(dir).filePath(name);
    const QFileInfo info(dest);
    if (info.exists() && info.isDir())
        return {};
    return dest;
}

QString DownloadQueue::torrentDir() const
{
    return m_libraryPath + QStringLiteral("/torrents");
}

QString DownloadQueue::collectionDir() const
{
    return m_libraryPath + QStringLiteral("/collections");
}

QString DownloadQueue::albumDir() const
{
    return m_libraryPath + QStringLiteral("/albums");
}

QString DownloadQueue::torrentJobId(const QString &url) const
{
    const QString name = filenameFromUrl(url);
    return QStringLiteral("torrent:") + (name.isEmpty() ? url : name);
}

bool DownloadQueue::maybeStartInAppTorrent(const QString &sourceUrl, const QString &torrentFile,
                                           const QString &savePath)
{
    if (!m_downloadInApp || !m_engine || !OfficialTorrentEngine::available())
        return false;
    QDir().mkpath(savePath);
    return m_engine->addOfficialTorrent(sourceUrl, torrentFile, savePath, torrentJobId(sourceUrl));
}

void DownloadQueue::start()
{
    if (m_running.exchange(true))
        return;
    JobGateLock lock(m_gate);
    if (!lock) {
        m_running = false;
        emit finished(false, tr("A catalog sync is already running. Finish or cancel it first."));
        return;
    }
    m_cancel = false;

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
    if (total == 0) {
        waitForTorrents();
        m_remixes.clear();
        m_albums.clear();
        m_running = false;
        emit finished(!m_cancel, m_cancel
                                     ? tr("Downloads cancelled.")
                                     : tr("Official torrents finished. Remaining items are already covered or on disk."));
        return;
    }

    downloadRemixes();

    int done = 0;
    const int albumTotal = m_albums.size();
    for (auto record : m_albums) {
        if (m_cancel)
            break;
        emit queueProgress(done, albumTotal, tr("Downloading album %1").arg(record.title));
        downloadAlbum(record);
        ++done;
        if (!m_cancel)
            QThread::msleep(static_cast<unsigned long>(m_delayMs));
    }

    waitForTorrents();

    m_remixes.clear();
    m_albums.clear();
    m_running = false;
    emit finished(!m_cancel, m_cancel ? tr("Downloads cancelled.") : tr("Downloads finished."));
}

void DownloadQueue::downloadRemixes()
{
    if (m_remixes.isEmpty())
        return;
    const int total = m_remixes.size();
    const int workers = std::clamp(m_concurrency, 1, 3);
    if (workers <= 1) {
        int done = 0;
        for (const auto &record : m_remixes) {
            if (m_cancel)
                break;
            emit queueProgress(done, total, tr("Downloading %1").arg(record.ocrId));
            downloadRemix(record);
            ++done;
            if (!m_cancel)
                QThread::msleep(static_cast<unsigned long>(m_delayMs));
        }
        return;
    }

    QList<QThread *> threads;
    QList<QObject *> contexts;
    std::atomic<int> nextIndex{0};
    std::atomic<int> finishedCount{0};
    std::atomic<int> active{workers};
    for (int i = 0; i < workers; ++i) {
        auto *thread = new QThread;
        auto *ctx = new QObject;
        ctx->moveToThread(thread);
        thread->start();
        threads.append(thread);
        contexts.append(ctx);
        QMetaObject::invokeMethod(ctx, [this, ctx, total, &nextIndex, &finishedCount, &active]() {
            while (!m_cancel) {
                const int index = nextIndex.fetch_add(1);
                if (index >= m_remixes.size())
                    break;
                const RemixRecord copy = m_remixes.at(index);
                downloadRemix(copy);
                const int done = finishedCount.fetch_add(1) + 1;
                emit queueProgress(done, total, tr("Downloading %1").arg(copy.ocrId));
                if (!m_cancel)
                    QThread::msleep(static_cast<unsigned long>(m_delayMs));
            }
            active.fetch_sub(1);
        }, Qt::QueuedConnection);
    }

    QEventLoop loop;
    QTimer tick;
    tick.setInterval(100);
    QObject::connect(&tick, &QTimer::timeout, &loop, [this, &active, &loop]() {
        if (active.load() <= 0)
            loop.quit();
        Q_UNUSED(this);
    });
    tick.start();
    loop.exec();
    for (int i = 0; i < threads.size(); ++i) {
        threads[i]->quit();
        threads[i]->wait(8000);
        delete contexts[i];
        delete threads[i];
    }
}

void DownloadQueue::waitForTorrents()
{
    if (m_cancel || !m_engine || !m_downloadInApp || !OfficialTorrentEngine::available())
        return;
    if (!m_engine->hasActiveTorrents())
        return;
    emit queueProgress(0, 0, tr("Waiting for official torrents to finish…"));
    QEventLoop loop;
    QTimer tick;
    tick.setInterval(250);
    QObject::connect(&tick, &QTimer::timeout, &loop, [this, &loop]() {
        if (m_cancel || !m_engine || !m_engine->hasActiveTorrents())
            loop.quit();
    });
    tick.start();
    loop.exec();
}

bool DownloadQueue::applyOfficialTorrents()
{
    HttpClient http;
    http.setCancelFlag(&m_cancel);
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
            bool seen = false;
            for (const auto &existing : collections) {
                if (existing.url == extra.url) {
                    seen = true;
                    break;
                }
            }
            if (seen)
                continue;
            const bool extendsCoverage = extra.rangeEnd > coveredEnd;
            const bool afterCoverage = extra.rangeStart > coveredEnd;
            if (!extendsCoverage && !afterCoverage && extra.rangeEnd > 0)
                continue;
            collections.push_back(extra);
            coveredEnd = qMax(coveredEnd, extra.rangeEnd);
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
        job.itemId = torrentJobId(collection.url);
        job.displayName = collection.name;
        job.status = QStringLiteral("running");
        emit jobUpdated(job);
        emit queueProgress(++step, torrentJobs, tr("Saving collection torrent %1").arg(collection.name));

        const QString dest = destForUrl(torrentDir(), collection.url);
        bool ok = false;
        if (dest.isEmpty()) {
            job.error = tr("Collection torrent URL has no file name.");
        } else {
            const bool existed = QFileInfo::exists(dest) && QFileInfo(dest).isFile() && QFileInfo(dest).size() > 0;
            ok = existed;
            if (!ok) {
                const auto result = http.downloadToFile(collection.url, dest);
                ok = result.ok;
                if (!ok)
                    job.error = result.error;
            }
            if (ok && !existed)
                saved << dest;
        }
        bool handedOff = false;
        if (ok) {
            if (m_downloadInApp && OfficialTorrentEngine::available())
                handedOff = maybeStartInAppTorrent(collection.url, dest, collectionDir());
            else if (m_openTorrents)
                handedOff = true;
            if (handedOff && collection.rangeEnd >= collection.rangeStart && collection.rangeEnd > 0) {
                covered.push_back({collection.rangeStart, collection.rangeEnd});
                const QString note = tr("Covered by official collection torrent: %1").arg(collection.name);
                emit remixRangeCovered(collection.rangeStart, collection.rangeEnd, note);
                job.status = QStringLiteral("done");
            } else if (m_downloadInApp && OfficialTorrentEngine::available() && !handedOff) {
                job.status = QStringLiteral("error");
                if (job.error.isEmpty())
                    job.error = tr("In-app torrent was rejected; those tracks will use HTTPS mirrors.");
            } else {
                job.status = QStringLiteral("done");
            }
        } else {
            job.status = QStringLiteral("error");
        }
        emit jobUpdated(job);
        QThread::msleep(static_cast<unsigned long>(m_delayMs));
    }

    if (!covered.isEmpty()) {
        std::sort(covered.begin(), covered.end(),
                  [](const Range &a, const Range &b) { return a.from < b.from; });
        QVector<Range> merged;
        for (const auto &range : covered) {
            if (merged.isEmpty() || range.from > merged.last().to + 1)
                merged.push_back(range);
            else
                merged.last().to = qMax(merged.last().to, range.to);
        }
        covered = merged;
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
    job.displayName = record.ocrId + QStringLiteral(" — ") + record.title;
    job.status = QStringLiteral("running");
    emit jobUpdated(job);

    HttpClient http;
    http.setCancelFlag(&m_cancel);
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
    QString dest;
    for (const auto &mirror : HtmlParsers::preferMirrors(record.mirrors)) {
        dest = destForUrl(destDir, mirror);
        if (!dest.isEmpty())
            break;
    }
    if (dest.isEmpty()) {
        record.status = QStringLiteral("error");
        record.lastError = QStringLiteral("Mirror URL has no file name");
        job.status = QStringLiteral("error");
        job.error = record.lastError;
        emit remixUpdated(record);
        emit jobUpdated(job);
        return false;
    }
    if (QFileInfo::exists(dest) && QFileInfo(dest).isFile() && QFileInfo(dest).size() > 0) {
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
        job.displayName = QFileInfo(dest).fileName();
        emit jobUpdated(job);
        const auto result = http.downloadToFile(mirror, dest, 120000, [&](qint64 done, qint64 total) {
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
        QFile::replace(dest);
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
    http.setCancelFlag(&m_cancel);
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

    const QString albumDirPath = albumDir();
    QDir().mkpath(albumDirPath);
    QDir().mkpath(torrentDir());

    auto saveTorrent = [&]() -> bool {
        if (record.torrentUrl.isEmpty())
            return false;
        const QString dest = destForUrl(torrentDir(), record.torrentUrl);
        if (dest.isEmpty()) {
            record.lastError = tr("Album torrent URL has no file name.");
            return false;
        }
        bool ok = QFileInfo::exists(dest) && QFileInfo(dest).isFile() && QFileInfo(dest).size() > 0;
        if (!ok) {
            const auto result = http.downloadToFile(record.torrentUrl, dest);
            ok = result.ok;
            if (!ok)
                record.lastError = result.error;
        }
        if (!ok)
            return false;
        bool handedOff = false;
        if (m_downloadInApp && OfficialTorrentEngine::available()) {
            handedOff = maybeStartInAppTorrent(record.torrentUrl, dest, albumDirPath);
            if (!handedOff) {
                record.status = QStringLiteral("error");
                record.lastError = tr("In-app album torrent was rejected.");
                return false;
            }
            record.localPath = albumDirPath;
            record.status = QStringLiteral("in-torrent");
            record.lastError = tr("Downloading official album torrent inside OCR Catalog.");
        } else if (m_openTorrents) {
            record.localPath = dest;
            record.status = QStringLiteral("in-torrent");
            record.lastError = tr("Saved official album torrent. Open it in a BitTorrent client "
                                  "(qBittorrent, Transmission, Deluge).");
            handedOff = true;
        } else {
            record.localPath = dest;
            record.status = QStringLiteral("available");
            record.lastError = tr("Saved official album torrent. Enable in-app torrents or "
                                  "open it in a BitTorrent client, or use HTTPS mirrors.");
        }
        job.kind = DownloadJob::Torrent;
        job.itemId = torrentJobId(record.torrentUrl);
        job.status = handedOff ? QStringLiteral("done") : QStringLiteral("saved");
        job.displayName = QFileInfo(dest).fileName();
        emit albumUpdated(record);
        emit jobUpdated(job);
        return handedOff;
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
        QString dest;
        for (const auto &mirror : HtmlParsers::preferMirrors(record.zipMirrors)) {
            dest = destForUrl(albumDirPath, mirror);
            if (!dest.isEmpty())
                break;
        }
        if (dest.isEmpty()) {
            record.lastError = tr("Album zip URL has no file name.");
        } else if (QFileInfo::exists(dest) && QFileInfo(dest).isFile() && QFileInfo(dest).size() > 0) {
            record.localPath = dest;
            record.status = QStringLiteral("downloaded");
            job.status = QStringLiteral("skipped");
            emit albumUpdated(record);
            emit jobUpdated(job);
            return true;
        } else {
            QString lastError;
            for (const auto &mirror : HtmlParsers::preferMirrors(record.zipMirrors)) {
                if (m_cancel)
                    break;
                const auto result = http.downloadToFile(mirror, dest, 180000, [&](qint64 done, qint64 total) {
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
