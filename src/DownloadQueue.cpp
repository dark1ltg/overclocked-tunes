#include "DownloadQueue.h"

#include "HtmlParsers.h"
#include "OfficialTorrentEngine.h"
#include "appinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace {

int ocrNumberFromFileName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("OCR(\\d{5,})"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

} // namespace

DownloadQueue::DownloadQueue(QObject *parent)
    : QObject(parent)
    , m_http(new HttpClient(this))
{
    m_http->setCancelFlag(&m_cancel);
}

void DownloadQueue::enqueueRemixes(const QVector<RemixRecord> &records)
{
    m_remixes.append(records);
}

void DownloadQueue::enqueueAlbums(const QVector<AlbumRecord> &records)
{
    m_albums.append(records);
}

void DownloadQueue::cancel()
{
    m_cancel = true;
    m_paused = false;
}

void DownloadQueue::pause()
{
    m_paused = true;
}

void DownloadQueue::resume()
{
    m_paused = false;
}

void DownloadQueue::cancelAndAbort()
{
    m_cancel = true;
    m_paused = false;
    if (m_http)
        m_http->abort();
    if (m_engine)
        m_engine->cancelActive();
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

QString DownloadQueue::albumJobId(int albumId) const
{
    return QStringLiteral("album:") + QString::number(albumId);
}

bool DownloadQueue::maybeStartInAppTorrent(const QString &sourceUrl, const QString &torrentFile,
                                           const QString &savePath, const QString &itemId)
{
    if (!m_downloadInApp || !m_engine || !OfficialTorrentEngine::available())
        return false;
    QDir().mkpath(savePath);
    const QString id = itemId.isEmpty() ? torrentJobId(sourceUrl) : itemId;
    return m_engine->addOfficialTorrent(sourceUrl, torrentFile, savePath, id);
}

void DownloadQueue::waitIfPaused()
{
    while (m_paused && !m_cancel) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(50);
    }
}

void DownloadQueue::politeDelay()
{
    if (m_cancel)
        return;
    QElapsedTimer timer;
    timer.start();
    while (!m_cancel && timer.elapsed() < m_delayMs) {
        waitIfPaused();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(40);
    }
}

void DownloadQueue::emitQueuedSnapshot()
{
    for (const auto &album : m_albums) {
        DownloadJob job;
        job.kind = DownloadJob::Torrent;
        job.itemId = albumJobId(album.albumId);
        job.displayName = album.title;
        job.status = QStringLiteral("queued");
        emit jobUpdated(job);
    }
    for (const auto &remix : m_remixes) {
        DownloadJob job;
        job.kind = DownloadJob::Remix;
        job.itemId = remix.ocrId;
        job.displayName = remix.ocrId + QStringLiteral(" — ") + remix.title;
        job.status = QStringLiteral("queued");
        emit jobUpdated(job);
    }
}

void DownloadQueue::pruneRemixesAlreadyOnDisk()
{
    QSet<int> haveNumbers;
    QSet<QString> haveFiles;
    const QStringList audio{QStringLiteral("*.mp3"), QStringLiteral("*.ogg"),
                            QStringLiteral("*.flac"), QStringLiteral("*.m4a")};
    auto scan = [&](const QString &root) {
        if (!QDir(root).exists())
            return;
        QDirIterator it(root, audio, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            haveFiles.insert(QFileInfo(path).fileName());
            const int number = ocrNumberFromFileName(QFileInfo(path).fileName());
            if (number > 0)
                haveNumbers.insert(number);
        }
    };
    scan(collectionDir());
    scan(m_libraryPath + QStringLiteral("/remixes"));
    scan(albumDir());

    QVector<RemixRecord> remaining;
    remaining.reserve(m_remixes.size());
    for (const auto &record : m_remixes) {
        if (record.status == QLatin1String("downloaded") || record.status == QLatin1String("unavailable")
            || record.status == QLatin1String("in-torrent")) {
            continue;
        }
        if (record.ocrNumber > 0 && haveNumbers.contains(record.ocrNumber)) {
            DownloadJob job;
            job.kind = DownloadJob::Remix;
            job.itemId = record.ocrId;
            job.displayName = record.ocrId + QStringLiteral(" — ") + record.title;
            job.status = QStringLiteral("skipped");
            job.error = tr("Already present from a prior torrent download.");
            emit jobUpdated(job);
            continue;
        }
        bool haveMirrorFile = false;
        for (const auto &mirror : record.mirrors) {
            const QString name = filenameFromUrl(mirror);
            if (!name.isEmpty() && haveFiles.contains(name)) {
                haveMirrorFile = true;
                break;
            }
        }
        if (haveMirrorFile) {
            DownloadJob job;
            job.kind = DownloadJob::Remix;
            job.itemId = record.ocrId;
            job.displayName = record.ocrId + QStringLiteral(" — ") + record.title;
            job.status = QStringLiteral("skipped");
            job.error = tr("Already present from a prior torrent download.");
            emit jobUpdated(job);
            continue;
        }
        remaining.push_back(record);
    }
    m_remixes = remaining;
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
    m_paused = false;
    m_http->setCancelFlag(&m_cancel);
    m_http->resetAbort();

    emitQueuedSnapshot();

    if (m_preferTorrents) {
        emit queueProgress(0, 0, tr("Starting the two official collection torrents (OCR 1–4000 and 4001–4473)…"));
        if (!applyOfficialTorrents() && m_cancel) {
            m_remixes.clear();
            m_albums.clear();
            m_running = false;
            emit finished(false, tr("Downloads cancelled."));
            return;
        }
        if (!m_cancel) {
            emit queueProgress(0, 0, tr("Waiting for the two official collection torrents to finish…"));
            waitForTorrents();
        }
        if (m_cancel) {
            m_remixes.clear();
            m_albums.clear();
            m_running = false;
            emit finished(false, tr("Downloads cancelled."));
            return;
        }
        pruneRemixesAlreadyOnDisk();
    }

    if (m_includeAlbums && !m_albums.isEmpty()) {
        int done = 0;
        const int albumTotal = m_albums.size();
        for (auto record : m_albums) {
            waitIfPaused();
            if (m_cancel)
                break;
            emit queueProgress(done, albumTotal,
                               tr("Album torrent %1 of %2: %3").arg(done + 1).arg(albumTotal).arg(record.title));
            downloadAlbum(record);
            waitForTorrents();
            ++done;
            if (!m_cancel)
                politeDelay();
        }
        if (!m_cancel)
            pruneRemixesAlreadyOnDisk();
    }

    if (!m_cancel)
        downloadRemixes();

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
    pruneRemixesAlreadyOnDisk();
    const int total = m_remixes.size();
    int done = 0;
    for (const auto &record : m_remixes) {
        waitIfPaused();
        if (m_cancel)
            break;
        emit queueProgress(done, total, tr("Downloading remaining track %1 of %2: %3")
                                            .arg(done + 1)
                                            .arg(total)
                                            .arg(record.ocrId));
        downloadRemix(record);
        ++done;
        if (!m_cancel)
            politeDelay();
    }
}

void DownloadQueue::waitForTorrents()
{
    if (m_cancel || !m_engine || !m_downloadInApp || !OfficialTorrentEngine::available())
        return;
    if (!m_engine->hasUnfinishedTorrents())
        return;
    emit queueProgress(0, 0, tr("Waiting for the current official torrent to finish…"));
    QEventLoop loop;
    QTimer tick;
    tick.setInterval(250);
    QObject::connect(&tick, &QTimer::timeout, &loop, [this, &loop]() {
        if (m_engine)
            m_engine->poll();
        if (m_cancel || !m_engine || !m_engine->hasUnfinishedTorrents())
            loop.quit();
    });
    tick.start();
    loop.exec();
}

bool DownloadQueue::applyOfficialTorrents()
{
    QVector<TorrentListing> collections = HtmlParsers::startingCollectionTorrents();
    QVector<TorrentListing> listings;

    const QString pageUrl = QStringLiteral("%1/torrents/").arg(QLatin1String(AppInfo::siteRoot()));
    const auto page = m_http->get(pageUrl);
    if (page.ok) {
        listings = HtmlParsers::parseTorrentList(QString::fromUtf8(page.body));
    } else if (!m_cancel) {
        emit queueProgress(0, 0,
                           tr("Could not read torrents page (%1); still using the two official collection torrents.")
                               .arg(page.error));
    }

    QDir().mkpath(torrentDir());
    QStringList saved;

    int step = 0;
    const int torrentJobs = qMax(collections.size(), 1);
    struct Range {
        int from = 0;
        int to = 0;
    };
    QVector<Range> covered;
    for (const auto &collection : collections) {
        waitIfPaused();
        if (m_cancel)
            return false;
        DownloadJob job;
        job.kind = DownloadJob::CollectionTorrent;
        job.itemId = torrentJobId(collection.url);
        job.displayName = collection.name;
        job.status = QStringLiteral("running");
        emit jobUpdated(job);
        emit queueProgress(++step, torrentJobs, tr("Starting collection torrent %1").arg(collection.name));

        const QString dest = destForUrl(torrentDir(), collection.url);
        bool ok = false;
        if (dest.isEmpty()) {
            job.error = tr("Collection torrent URL has no file name.");
        } else {
            const bool existed = QFileInfo::exists(dest) && QFileInfo(dest).isFile() && QFileInfo(dest).size() > 0;
            ok = existed;
            if (!ok) {
                const auto result = m_http->downloadToFile(collection.url, dest);
                ok = result.ok;
                if (!ok)
                    job.error = result.error;
            }
            if (ok && !existed)
                saved << dest;
        }
        bool handedOff = false;
        if (ok) {
            if (m_downloadInApp && OfficialTorrentEngine::available()) {
                handedOff = maybeStartInAppTorrent(collection.url, dest, collectionDir(), job.itemId);
                if (handedOff) {
                    job.status = QStringLiteral("running");
                } else {
                    job.status = QStringLiteral("error");
                    if (job.error.isEmpty())
                        job.error = tr("In-app torrent was rejected; those tracks will use HTTPS mirrors.");
                }
            } else if (m_openTorrents) {
                handedOff = true;
                job.status = QStringLiteral("done");
            } else {
                job.status = QStringLiteral("done");
            }
            if (handedOff && collection.rangeEnd >= collection.rangeStart && collection.rangeEnd > 0) {
                covered.push_back({collection.rangeStart, collection.rangeEnd});
                const QString note = tr("Covered by official collection torrent: %1").arg(collection.name);
                emit remixRangeCovered(collection.rangeStart, collection.rangeEnd, note);
            }
        } else {
            job.status = QStringLiteral("error");
        }
        emit jobUpdated(job);
        politeDelay();
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
        if (inRange) {
            DownloadJob job;
            job.kind = DownloadJob::Remix;
            job.itemId = r.ocrId;
            job.displayName = r.ocrId + QStringLiteral(" — ") + r.title;
            job.status = QStringLiteral("skipped");
            job.error = tr("Covered by an official collection torrent.");
            emit jobUpdated(job);
            continue;
        }
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

    if (record.mirrors.isEmpty()) {
        const auto page = m_http->get(record.pageUrl);
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
        job.error = tr("Already on disk.");
        emit remixUpdated(record);
        emit jobUpdated(job);
        return true;
    }

    QString lastError;
    for (const auto &mirror : HtmlParsers::preferMirrors(record.mirrors)) {
        waitIfPaused();
        if (m_cancel)
            break;
        job.displayName = QFileInfo(dest).fileName();
        emit jobUpdated(job);
        const auto result = m_http->downloadToFile(mirror, dest, 120000, [&](qint64 done, qint64 total) {
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
        if (!m_cancel)
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
    job.kind = DownloadJob::Torrent;
    job.itemId = albumJobId(record.albumId);
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

    if (record.torrentUrl.isEmpty() || record.httpHomepage.isEmpty()) {
        const auto page = m_http->get(record.pageUrl);
        if (page.ok) {
            HtmlParsers::parseAlbumDetail(&record, QString::fromUtf8(page.body));
            record.torrentUrl = HtmlParsers::preferHttps(record.torrentUrl);
            emit albumUpdated(record);
        }
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
            const auto result = m_http->downloadToFile(record.torrentUrl, dest);
            ok = result.ok;
            if (!ok)
                record.lastError = result.error;
        }
        if (!ok)
            return false;
        bool handedOff = false;
        if (m_downloadInApp && OfficialTorrentEngine::available()) {
            handedOff = maybeStartInAppTorrent(record.torrentUrl, dest, albumDirPath, job.itemId);
            if (!handedOff) {
                record.status = QStringLiteral("error");
                record.lastError = tr("In-app album torrent was rejected.");
                return false;
            }
            record.localPath = albumDirPath;
            record.status = QStringLiteral("in-torrent");
            record.lastError = tr("Downloading official album torrent inside OCR Catalog.");
            job.status = QStringLiteral("running");
        } else if (m_openTorrents) {
            record.localPath = dest;
            record.status = QStringLiteral("in-torrent");
            record.lastError = tr("Saved official album torrent. Open it in a BitTorrent client "
                                  "(qBittorrent, Transmission, Deluge).");
            handedOff = true;
            job.status = QStringLiteral("done");
        } else {
            record.localPath = dest;
            record.status = QStringLiteral("available");
            record.lastError = tr("Saved official album torrent. Enable in-app torrents or "
                                  "open it in a BitTorrent client, or use HTTPS mirrors.");
            job.status = QStringLiteral("saved");
        }
        job.kind = DownloadJob::Torrent;
        job.displayName = QFileInfo(dest).fileName();
        emit albumUpdated(record);
        emit jobUpdated(job);
        return handedOff;
    };

    if (m_preferTorrents && saveTorrent())
        return true;

    if (record.zipMirrors.isEmpty() && !record.httpHomepage.isEmpty()) {
        const auto home = m_http->get(record.httpHomepage);
        if (home.ok)
            record.zipMirrors = HtmlParsers::parseAlbumZipMirrors(QString::fromUtf8(home.body));
        emit albumUpdated(record);
    }

    if (!record.zipMirrors.isEmpty()) {
        job.kind = DownloadJob::AlbumZip;
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
                waitIfPaused();
                if (m_cancel)
                    break;
                const auto result = m_http->downloadToFile(mirror, dest, 180000, [&](qint64 done, qint64 total) {
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
                if (!m_cancel)
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
