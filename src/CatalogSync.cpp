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

void CatalogSync::start()
{
    m_cancel = false;
    emit progress(0, 0, tr("Fetching ReMix listings…"));
    if (!fetchPages(QStringLiteral("%1/remixes/?offset=%2&sort=datedesc").arg(QLatin1String(AppInfo::siteRoot())), false))
        return;
    if (m_cancel) {
        emit finished(false, tr("Catalog sync cancelled."));
        return;
    }
    emit progress(0, 0, tr("Fetching album listings…"));
    if (!fetchPages(QStringLiteral("%1/albums/?offset=%2&sort=datedesc").arg(QLatin1String(AppInfo::siteRoot())), true))
        return;
    if (m_cancel)
        emit finished(false, tr("Catalog sync cancelled."));
    else
        emit finished(true, tr("Catalog sync finished."));
}

bool CatalogSync::fetchPages(const QString &listUrl, bool albums)
{
    HttpClient http;
    const QString firstUrl = listUrl.arg(0);
    const auto first = http.get(firstUrl);
    if (!first.ok) {
        emit finished(false, tr("Failed to fetch %1: %2").arg(firstUrl, first.error));
        return false;
    }
    const QString html = QString::fromUtf8(first.body);
    const int count = HtmlParsers::parseListedCount(html, albums ? QStringLiteral("Albums") : QStringLiteral("ReMixes"));
    int lastOffset = HtmlParsers::parseLastOffset(html);
    if (lastOffset == 0 && count > 30)
        lastOffset = ((count - 1) / 30) * 30;

    auto emitPage = [&](const QString &pageHtml) {
        if (albums)
            emit albumsParsed(HtmlParsers::parseAlbumList(pageHtml));
        else
            emit remixesParsed(HtmlParsers::parseRemixList(pageHtml));
    };
    emitPage(html);

    int pages = lastOffset / 30 + 1;
    if (pages < 1)
        pages = 1;
    int done = 1;
    emit progress(done, pages, albums ? tr("Album page 1 of %1").arg(pages) : tr("ReMix page 1 of %1").arg(pages));

    for (int offset = 30; offset <= lastOffset; offset += 30) {
        if (m_cancel)
            return true;
        QThread::msleep(static_cast<unsigned long>(m_delayMs));
        const QString url = listUrl.arg(offset);
        const auto page = http.get(url);
        if (!page.ok) {
            emit finished(false, tr("Failed to fetch %1: %2").arg(url, page.error));
            return false;
        }
        emitPage(QString::fromUtf8(page.body));
        ++done;
        emit progress(done, pages,
                      albums ? tr("Album page %1 of %2").arg(done).arg(pages)
                             : tr("ReMix page %1 of %2").arg(done).arg(pages));
    }
    Q_UNUSED(count);
    return true;
}
