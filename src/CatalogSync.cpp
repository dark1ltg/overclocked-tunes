#include "CatalogSync.h"

#include "HtmlParsers.h"
#include "HttpClient.h"
#include "appinfo.h"

#include <QThread>
#include <QUrl>

CatalogSync::CatalogSync(QObject *parent)
    : QObject(parent)
{
}

void CatalogSync::cancel()
{
    m_cancel = true;
}

void CatalogSync::start()
{
    if (m_running.exchange(true))
        return;
    JobGateLock lock(m_gate);
    if (!lock) {
        m_running = false;
        emit finished(false, tr("A download is already running. Finish or cancel it first."));
        return;
    }
    m_cancel = false;

    int remixGot = 0;
    int remixListed = 0;
    int albumGot = 0;
    int albumListed = 0;

    emit progress(0, 0, tr("Fetching ReMix listings…"));
    if (!fetchPages(QStringLiteral("%1/remixes/?offset=%2&sort=datedesc").arg(QLatin1String(AppInfo::siteRoot())),
                    false, &remixGot, &remixListed)) {
        m_running = false;
        return;
    }
    if (m_cancel) {
        m_running = false;
        emit finished(false, tr("Catalog sync cancelled."));
        return;
    }
    emit progress(0, 0, tr("Fetching album listings…"));
    if (!fetchPages(QStringLiteral("%1/albums/?offset=%2&sort=datedesc").arg(QLatin1String(AppInfo::siteRoot())),
                    true, &albumGot, &albumListed)) {
        m_running = false;
        return;
    }
    if (m_cancel) {
        m_running = false;
        emit finished(false, tr("Catalog sync cancelled."));
        return;
    }

    QString message = tr("Catalog sync finished.");
    if (remixListed > 0 && remixGot + 5 < remixListed) {
        message = tr("Catalog sync finished, but only %1 of about %2 ReMixes were parsed. "
                     "The site layout may have changed.")
                      .arg(remixGot)
                      .arg(remixListed);
    }
    m_running = false;
    emit finished(true, message);
}

bool CatalogSync::fetchPages(const QString &listUrl, bool albums, int *outGot, int *outListed)
{
    HttpClient http;
    http.setCancelFlag(&m_cancel);
    const QString firstUrl = listUrl.arg(0);
    const auto first = http.get(firstUrl);
    if (m_cancel) {
        emit finished(false, tr("Catalog sync cancelled."));
        return false;
    }
    if (!first.ok) {
        emit finished(false, tr("Failed to fetch %1: %2").arg(firstUrl, first.error));
        return false;
    }
    const QString html = QString::fromUtf8(first.body);
    const int count = HtmlParsers::parseListedCount(html, albums ? QStringLiteral("Albums") : QStringLiteral("ReMixes"));
    if (outListed)
        *outListed = count;

    auto emitPage = [&](const QString &pageHtml) -> int {
        if (albums) {
            const auto records = HtmlParsers::parseAlbumList(pageHtml);
            emit albumsParsed(records);
            return records.size();
        }
        const auto records = HtmlParsers::parseRemixList(pageHtml);
        emit remixesParsed(records);
        return records.size();
    };

    int got = emitPage(html);
    int pageSize = got > 0 ? got : 30;
    int lastOffset = HtmlParsers::parseLastOffset(html);
    if (lastOffset == 0 && count > pageSize)
        lastOffset = ((count - 1) / pageSize) * pageSize;

    int pages = lastOffset / pageSize + 1;
    if (pages < 1)
        pages = 1;
    int done = 1;
    emit progress(done, pages, albums ? tr("Album page 1 of %1").arg(pages) : tr("ReMix page 1 of %1").arg(pages));

    auto fetchOffset = [&](int offset) -> int {
        if (m_cancel)
            return -1;
        QThread::msleep(static_cast<unsigned long>(m_delayMs));
        const QString url = listUrl.arg(offset);
        const auto page = http.get(url);
        if (m_cancel)
            return -1;
        if (!page.ok) {
            emit finished(false, tr("Failed to fetch %1: %2").arg(url, page.error));
            return -2;
        }
        return emitPage(QString::fromUtf8(page.body));
    };

    bool lastWasFull = got >= pageSize;
    for (int offset = pageSize; offset <= lastOffset; offset += pageSize) {
        const int n = fetchOffset(offset);
        if (n == -1)
            return true;
        if (n == -2)
            return false;
        got += n;
        lastWasFull = n >= pageSize;
        ++done;
        emit progress(done, qMax(pages, done),
                      albums ? tr("Album page %1 of %2").arg(done).arg(qMax(pages, done))
                             : tr("ReMix page %1 of %2").arg(done).arg(qMax(pages, done)));
    }

    int extraOffset = lastOffset > 0 ? lastOffset + pageSize : pageSize;
    const int safety = count > 0 ? count + pageSize : 20000;
    while (lastWasFull && extraOffset < safety && !m_cancel) {
        const int n = fetchOffset(extraOffset);
        if (n == -1)
            return true;
        if (n == -2)
            return false;
        got += n;
        lastWasFull = n >= pageSize;
        ++done;
        extraOffset += pageSize;
        emit progress(done, done,
                      albums ? tr("Album page %1").arg(done) : tr("ReMix page %1").arg(done));
    }

    if (outGot)
        *outGot = got;
    return true;
}
