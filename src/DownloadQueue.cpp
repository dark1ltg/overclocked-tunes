#include "DownloadQueue.h"

#include "HtmlParsers.h"
#include "HttpClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
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

void DownloadQueue::start()
{
    m_cancel = false;
    m_running = true;
    HttpClient http;
    const int total = m_remixes.size() + m_albums.size();
    int done = 0;
    if (total == 0) {
        m_running = false;
        emit finished(true, tr("Nothing to download."));
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

bool DownloadQueue::downloadRemix(RemixRecord record)
{
    DownloadJob job;
    job.kind = DownloadJob::Remix;
    job.itemId = record.ocrId;
    job.displayName = record.ocrId + QStringLiteral(" — ") + record.title;
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
            emit albumUpdated(record);
        }
    }
    if (record.zipMirrors.isEmpty() && !record.httpHomepage.isEmpty()) {
        const auto home = http.get(record.httpHomepage);
        if (home.ok)
            record.zipMirrors = HtmlParsers::parseAlbumZipMirrors(QString::fromUtf8(home.body));
        emit albumUpdated(record);
    }

    const QString albumDir = m_libraryPath + QStringLiteral("/albums");
    const QString torrentDir = m_libraryPath + QStringLiteral("/torrents");
    QDir().mkpath(albumDir);
    QDir().mkpath(torrentDir);

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

    if (!record.torrentUrl.isEmpty()) {
        const QString torrentName = filenameFromUrl(record.torrentUrl);
        const QString dest = torrentDir + QLatin1Char('/') + torrentName;
        const auto result = http.downloadToFile(record.torrentUrl, dest);
        if (result.ok) {
            if (record.localPath.isEmpty())
                record.localPath = dest;
            if (record.status != QStringLiteral("downloaded"))
                record.status = QStringLiteral("available");
            record.lastError = record.zipMirrors.isEmpty()
                ? tr("Saved .torrent (no ZIP mirrors found). Open it in a BitTorrent client.")
                : record.lastError;
            job.kind = DownloadJob::Torrent;
            job.status = QStringLiteral("done");
            job.displayName = torrentName;
            emit albumUpdated(record);
            emit jobUpdated(job);
            return record.status == QStringLiteral("downloaded");
        }
        record.lastError = result.error;
    }

    if (record.status != QStringLiteral("downloaded"))
        record.status = QStringLiteral("error");
    job.status = QStringLiteral("error");
    job.error = record.lastError;
    emit albumUpdated(record);
    emit jobUpdated(job);
    return false;
}
