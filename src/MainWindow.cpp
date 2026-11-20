#include "MainWindow.h"

#include "CatalogSync.h"
#include "DesktopIntegrator.h"
#include "DownloadQueue.h"
#include "OfficialTorrentEngine.h"
#include "Settings.h"
#include "appinfo.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#ifdef OCRCATALOG_HAS_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromLatin1(AppInfo::name()));
    setWindowIcon(QIcon(QStringLiteral(":/icons/org.ocrcatalog.OCRCatalog.svg")));
    resize(1200, 760);
    buildUi();

#ifdef OCRCATALOG_HAS_MULTIMEDIA
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
#endif
}

bool MainWindow::ready()
{
    if (!m_store.isOpen() && !m_store.open()) {
        const QString error = m_store.lastError();
        qWarning() << "OCR Catalog database:" << error;
        QMessageBox::critical(this, tr("OCR Catalog"),
                              tr("Could not open the catalog database.\n\n%1").arg(error));
        return false;
    }
    if (!m_workersStarted)
        setupWorkers();
    reloadTables();
    updateStatus();
    QTimer::singleShot(600, this, &MainWindow::maybeOfferMenuInstall);
    return true;
}

void MainWindow::setupWorkers()
{
    m_sync = new CatalogSync;
    m_sync->moveToThread(&m_worker);
    m_downloads = new DownloadQueue;
    m_downloads->moveToThread(&m_worker);
    m_torrentEngine = new OfficialTorrentEngine;
    m_torrentEngine->moveToThread(&m_worker);
    m_downloads->setTorrentEngine(m_torrentEngine);
    m_sync->setJobGate(&m_jobGate);
    m_downloads->setJobGate(&m_jobGate);
    connect(m_sync, &CatalogSync::remixesParsed, this, &MainWindow::onRemixesParsed);
    connect(m_sync, &CatalogSync::albumsParsed, this, &MainWindow::onAlbumsParsed);
    connect(m_sync, &CatalogSync::progress, this, [this](int current, int total, const QString &message) {
        m_progress->setMaximum(total > 0 ? total : 0);
        m_progress->setValue(current);
        m_statusLabel->setText(message);
    });
    connect(m_sync, &CatalogSync::finished, this, &MainWindow::onSyncFinished);
    connect(m_downloads, &DownloadQueue::remixUpdated, this, [this](const RemixRecord &r) {
        m_store.updateRemix(r);
        applyRemixToTable(r);
        updateStatus();
    });
    connect(m_downloads, &DownloadQueue::albumUpdated, this, [this](const AlbumRecord &a) {
        m_store.updateAlbum(a);
        applyAlbumToTable(a);
        updateStatus();
    });
    connect(m_downloads, &DownloadQueue::jobUpdated, this, &MainWindow::applyJobToQueueTable);
    connect(m_torrentEngine, &OfficialTorrentEngine::jobUpdated, this, &MainWindow::applyJobToQueueTable);
    connect(m_torrentEngine, &OfficialTorrentEngine::torrentFailed, this,
            [this](const QString &name, const QString &error) {
                DownloadJob job;
                job.kind = DownloadJob::CollectionTorrent;
                job.itemId = QStringLiteral("torrent:") + name;
                job.displayName = name;
                job.status = QStringLiteral("error");
                job.error = error;
                applyJobToQueueTable(job);
            });
    connect(m_torrentEngine, &OfficialTorrentEngine::torrentCompleted, this, &MainWindow::onTorrentCompleted);
    connect(m_downloads, &DownloadQueue::queueProgress, this, [this](int current, int total, const QString &message) {
        m_progress->setMaximum(total > 0 ? total : 0);
        m_progress->setValue(current);
        m_statusLabel->setText(message);
    });
    connect(m_downloads, &DownloadQueue::finished, this, &MainWindow::onDownloadFinished);
    connect(m_downloads, &DownloadQueue::remixRangeCovered, this, &MainWindow::onRemixRangeCovered);
    connect(m_downloads, &DownloadQueue::torrentsReady, this, &MainWindow::onTorrentsReady);
    connect(m_downloads, &DownloadQueue::coverageSummary, this, &MainWindow::onCoverageSummary);
    m_worker.start();
    m_workersStarted = true;
}

void MainWindow::shutdownWorkers()
{
    if (m_sync)
        m_sync->cancel();
    if (m_downloads)
        m_downloads->cancel();
    if (m_worker.isRunning() && m_torrentEngine) {
        QMetaObject::invokeMethod(m_torrentEngine, "cancelActive", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_torrentEngine, "stop", Qt::BlockingQueuedConnection);
    }
    if (m_worker.isRunning()) {
        m_worker.quit();
        if (!m_worker.wait(60000))
            qWarning() << "OCR Catalog worker did not exit; leaving it to the process so libtorrent is not terminated.";
    }
    auto reclaim = [this](QObject *obj) {
        if (!obj)
            return;
        obj->moveToThread(QThread::currentThread());
        delete obj;
    };
    if (!m_worker.isRunning()) {
        reclaim(m_sync);
        m_sync = nullptr;
        reclaim(m_downloads);
        m_downloads = nullptr;
        reclaim(m_torrentEngine);
        m_torrentEngine = nullptr;
    } else {
        m_sync = nullptr;
        m_downloads = nullptr;
        m_torrentEngine = nullptr;
    }
    m_workersStarted = false;
}

MainWindow::~MainWindow()
{
    shutdownWorkers();
}

void MainWindow::buildUi()
{
    auto *tool = addToolBar(tr("Main"));
    tool->setMovable(false);
    tool->addAction(tr("Library…"), this, &MainWindow::chooseLibrary);
    tool->addAction(tr("Sync catalog"), this, &MainWindow::syncCatalog);
    tool->addAction(tr("Download missing"), this, &MainWindow::downloadMissing);
    tool->addAction(tr("Download selected"), this, &MainWindow::downloadSelected);
    tool->addAction(tr("Cancel"), this, &MainWindow::pauseDownloads);
    tool->addAction(tr("Play"), this, &MainWindow::playSelected);
    tool->addSeparator();
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search title, game, artist, OCR ID…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(280);
    tool->addWidget(m_search);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("Choose library folder…"), this, &MainWindow::chooseLibrary);
    fileMenu->addAction(tr("Settings…"), this, &MainWindow::openSettings);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quit"), this, &QWidget::close);
    auto *editMenu = menuBar()->addMenu(tr("&Catalog"));
    editMenu->addAction(tr("Sync catalog"), this, &MainWindow::syncCatalog);
    editMenu->addAction(tr("Download missing"), this, &MainWindow::downloadMissing);
    auto *desktopMenu = menuBar()->addMenu(tr("&Desktop"));
    desktopMenu->addAction(tr("Install to application menu"), this, &MainWindow::installMenu);
    desktopMenu->addAction(tr("Remove from application menu"), this, &MainWindow::removeMenu);
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Open OC ReMix"), this, []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://ocremix.org/"))); });
    helpMenu->addAction(tr("About"), this, [this]() {
        QMessageBox::about(this, tr("About OCR Catalog"),
                           tr("<b>OCR Catalog</b> %1<p>Catalog and download freely published "
                              "OC ReMix songs and albums from ocremix.org.</p>"
                              "<p>Download missing starts with the two official collection torrents "
                              "(OCR 1–4000 and 4001–4473), then fetches only newer remixes and "
                              "albums those packs do not cover from the site's HTTPS mirrors.</p>"
                              "<p>%2</p>")
                               .arg(QLatin1String(AppInfo::version()),
                                    OfficialTorrentEngine::available()
                                        ? tr("Linux builds download those official torrents inside this app. "
                                             "Only files from bt.ocremix.org are accepted; there is no way to add "
                                             "other torrents or magnets.")
                                        : tr("This build does not include an in-app torrent engine. "
                                             "Save the official .torrent files and open them in a BitTorrent client.")));
    });

    m_tabs = new QTabWidget;
    m_remixTable = new QTableWidget(0, 6);
    m_remixTable->setHorizontalHeaderLabels({tr("Status"), tr("ID"), tr("Title"), tr("Game"), tr("Artists"), tr("Posted")});
    m_remixTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remixTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remixTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remixTable->setAlternatingRowColors(true);
    m_remixTable->setSortingEnabled(true);
    m_remixTable->horizontalHeader()->setStretchLastSection(true);
    m_remixTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_remixTable->verticalHeader()->setVisible(false);
    connect(m_remixTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onRemixSelectionChanged);

    m_albumTable = new QTableWidget(0, 6);
    m_albumTable->setHorizontalHeaderLabels({tr("Status"), tr("Catalog"), tr("Title"), tr("Classification"), tr("Released"), tr("Downloadable")});
    m_albumTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_albumTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_albumTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_albumTable->setAlternatingRowColors(true);
    m_albumTable->setSortingEnabled(true);
    m_albumTable->horizontalHeader()->setStretchLastSection(true);
    m_albumTable->verticalHeader()->setVisible(false);
    connect(m_albumTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onAlbumSelectionChanged);

    m_queueTable = new QTableWidget(0, 4);
    m_queueTable->setHorizontalHeaderLabels({tr("Item"), tr("Name"), tr("State"), tr("Progress")});
    m_queueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_queueTable->horizontalHeader()->setStretchLastSection(true);
    m_queueTable->verticalHeader()->setVisible(false);

    auto *remixPage = new QWidget;
    auto *remixLayout = new QVBoxLayout(remixPage);
    remixLayout->setContentsMargins(0, 0, 0, 0);
    remixLayout->addWidget(m_remixTable);
    auto *albumPage = new QWidget;
    auto *albumLayout = new QVBoxLayout(albumPage);
    albumLayout->setContentsMargins(0, 0, 0, 0);
    albumLayout->addWidget(m_albumTable);

    m_tabs->addTab(remixPage, tr("ReMixes"));
    m_tabs->addTab(albumPage, tr("Albums"));
    m_tabs->addTab(m_queueTable, tr("Queue"));

    m_detail = new QTextEdit;
    m_detail->setReadOnly(true);
    m_detail->setPlaceholderText(tr("Select a ReMix or album to see details."));

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_tabs);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto *emptyHint = new QLabel(
        tr("No catalog yet. Choose a library folder, then click Sync catalog.\n"
           "Download missing starts with the two official collection torrents "
           "(OCR 1–4000 and 4001–4473), then HTTPS mirrors for newer remixes and albums."));
    emptyHint->setObjectName(QStringLiteral("emptyHint"));
    emptyHint->setWordWrap(true);
    emptyHint->setAlignment(Qt::AlignCenter);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(emptyHint);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);
    emptyHint->setVisible(m_store.remixCount() == 0);

    m_statusLabel = new QLabel(tr("Ready"));
    m_progress = new QProgressBar;
    m_progress->setMaximumWidth(220);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(14);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progress);
}

void MainWindow::reloadTables()
{
    fillRemixTable();
    fillAlbumTable();
    const bool empty = m_store.remixCount() == 0;
    if (auto *hint = findChild<QLabel *>(QStringLiteral("emptyHint")))
        hint->setVisible(empty);
}

QTableWidgetItem *MainWindow::taggedItem(const QString &text, const QString &id) const
{
    auto *item = new QTableWidgetItem(text);
    item->setData(Qt::UserRole, id);
    return item;
}

void MainWindow::fillRemixTable()
{
    const bool sorting = m_remixTable->isSortingEnabled();
    m_remixTable->setSortingEnabled(false);
    m_remixCache = m_store.remixes(m_search->text());
    m_remixTable->setRowCount(m_remixCache.size());
    for (int i = 0; i < m_remixCache.size(); ++i) {
        const auto &r = m_remixCache[i];
        m_remixTable->setItem(i, 0, taggedItem(r.status, r.ocrId));
        m_remixTable->setItem(i, 1, taggedItem(r.ocrId, r.ocrId));
        m_remixTable->setItem(i, 2, taggedItem(r.title, r.ocrId));
        m_remixTable->setItem(i, 3, taggedItem(r.game, r.ocrId));
        m_remixTable->setItem(i, 4, taggedItem(r.artists, r.ocrId));
        m_remixTable->setItem(i, 5, taggedItem(r.posted, r.ocrId));
    }
    m_remixTable->setSortingEnabled(sorting);
    m_tabs->setTabText(0, tr("ReMixes (%1)").arg(m_store.remixCount()));
}

void MainWindow::fillAlbumTable()
{
    const bool sorting = m_albumTable->isSortingEnabled();
    m_albumTable->setSortingEnabled(false);
    m_albumCache = m_store.albums(m_search->text());
    m_albumTable->setRowCount(m_albumCache.size());
    for (int i = 0; i < m_albumCache.size(); ++i) {
        const auto &a = m_albumCache[i];
        const QString id = QString::number(a.albumId);
        m_albumTable->setItem(i, 0, taggedItem(a.status, id));
        m_albumTable->setItem(i, 1, taggedItem(a.catalog, id));
        m_albumTable->setItem(i, 2, taggedItem(a.title, id));
        m_albumTable->setItem(i, 3, taggedItem(a.classification, id));
        m_albumTable->setItem(i, 4, taggedItem(a.released, id));
        m_albumTable->setItem(i, 5, taggedItem(a.ocrPublished ? tr("Yes") : tr("Catalog only"), id));
    }
    m_albumTable->setSortingEnabled(sorting);
    m_tabs->setTabText(1, tr("Albums (%1)").arg(m_store.albumCount()));
}

void MainWindow::applyRemixToTable(const RemixRecord &record)
{
    for (auto &cached : m_remixCache) {
        if (cached.ocrId == record.ocrId) {
            cached = record;
            break;
        }
    }
    for (int i = 0; i < m_remixTable->rowCount(); ++i) {
        auto *item = m_remixTable->item(i, 1);
        if (!item || item->data(Qt::UserRole).toString() != record.ocrId)
            continue;
        m_remixTable->item(i, 0)->setText(record.status);
        m_remixTable->item(i, 2)->setText(record.title);
        m_remixTable->item(i, 3)->setText(record.game);
        m_remixTable->item(i, 4)->setText(record.artists);
        m_remixTable->item(i, 5)->setText(record.posted);
        return;
    }
}

void MainWindow::applyAlbumToTable(const AlbumRecord &record)
{
    for (auto &cached : m_albumCache) {
        if (cached.albumId == record.albumId) {
            cached = record;
            break;
        }
    }
    const QString id = QString::number(record.albumId);
    for (int i = 0; i < m_albumTable->rowCount(); ++i) {
        auto *item = m_albumTable->item(i, 0);
        if (!item || item->data(Qt::UserRole).toString() != id)
            continue;
        m_albumTable->item(i, 0)->setText(record.status);
        m_albumTable->item(i, 1)->setText(record.catalog);
        m_albumTable->item(i, 2)->setText(record.title);
        m_albumTable->item(i, 3)->setText(record.classification);
        m_albumTable->item(i, 4)->setText(record.released);
        return;
    }
}

RemixRecord MainWindow::remixAtRow(int row) const
{
    auto *item = m_remixTable->item(row, 1);
    const QString id = item ? item->data(Qt::UserRole).toString() : QString();
    for (const auto &r : m_remixCache) {
        if (r.ocrId == id)
            return r;
    }
    return id.isEmpty() ? RemixRecord{} : m_store.remix(id);
}

AlbumRecord MainWindow::albumAtRow(int row) const
{
    auto *item = m_albumTable->item(row, 0);
    const int id = item ? item->data(Qt::UserRole).toInt() : 0;
    for (const auto &a : m_albumCache) {
        if (a.albumId == id)
            return a;
    }
    return id == 0 ? AlbumRecord{} : m_store.album(id);
}

void MainWindow::updateStatus()
{
    m_statusLabel->setText(
        tr("%1 ReMixes (%2 downloaded, %3 via torrent, %4 remaining) · %5 albums "
           "(%6 downloaded, %7 via torrent) · library: %8")
            .arg(m_store.remixCount())
            .arg(m_store.remixDownloadedCount())
            .arg(m_store.remixTorrentCoveredCount())
            .arg(m_store.remixNeedingCount())
            .arg(m_store.albumCount())
            .arg(m_store.albumDownloadedCount())
            .arg(m_store.albumTorrentCoveredCount())
            .arg(Settings::instance().libraryPath().isEmpty()
                     ? tr("(not set)")
                     : Settings::instance().libraryPath()));
}

void MainWindow::ensureLibrary()
{
    if (Settings::instance().libraryPath().isEmpty())
        chooseLibrary();
}

void MainWindow::applyJobToQueueTable(const DownloadJob &job)
{
    int row = -1;
    for (int i = 0; i < m_queueTable->rowCount(); ++i) {
        if (m_queueTable->item(i, 0) && m_queueTable->item(i, 0)->data(Qt::UserRole).toString() == job.itemId) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        row = m_queueTable->rowCount();
        m_queueTable->insertRow(row);
        auto *idItem = new QTableWidgetItem(job.itemId);
        idItem->setData(Qt::UserRole, job.itemId);
        m_queueTable->setItem(row, 0, idItem);
    }
    m_queueTable->setItem(row, 1, new QTableWidgetItem(job.displayName));
    m_queueTable->setItem(row, 2, new QTableWidgetItem(job.status));
    const QString prog = job.bytesTotal > 0
        ? QStringLiteral("%1 / %2 MB").arg(job.bytesDone / 1048576.0, 0, 'f', 1).arg(job.bytesTotal / 1048576.0, 0, 'f', 1)
        : QString();
    m_queueTable->setItem(row, 3, new QTableWidgetItem(prog.isEmpty() ? job.error : prog));
}

void MainWindow::chooseLibrary()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Library folder"), Settings::instance().libraryPath());
    if (dir.isEmpty())
        return;
    Settings::instance().setLibraryPath(dir);
    QDir().mkpath(dir + QStringLiteral("/remixes"));
    QDir().mkpath(dir + QStringLiteral("/albums"));
    QDir().mkpath(dir + QStringLiteral("/torrents"));
    QDir().mkpath(dir + QStringLiteral("/collections"));
    m_store.markExistingFiles(dir);
    reloadTables();
    updateStatus();
}

void MainWindow::syncCatalog()
{
    if ((m_sync && m_sync->isRunning()) || (m_downloads && m_downloads->isRunning()) || m_jobGate.locked()) {
        QMessageBox::information(this, tr("Sync"),
                                 tr("Wait for the current sync or download to finish (or cancel it) first."));
        return;
    }
    m_progress->setRange(0, 0);
    m_statusLabel->setText(tr("Syncing catalog from ocremix.org…"));
    const int delay = Settings::instance().requestDelayMs();
    QMetaObject::invokeMethod(m_sync, [this, delay]() {
        m_sync->setDelayMs(delay);
        m_sync->start();
    }, Qt::QueuedConnection);
}

void MainWindow::downloadMissing()
{
    if ((m_downloads && m_downloads->isRunning()) || (m_sync && m_sync->isRunning()) || m_jobGate.locked()) {
        QMessageBox::information(this, tr("Downloads"),
                                 tr("Wait for the current sync or download to finish (or cancel it) first."));
        return;
    }
    ensureLibrary();
    if (Settings::instance().libraryPath().isEmpty())
        return;
    auto remixes = m_store.remixesNeedingDownload();
    auto albums = Settings::instance().includeAlbums() ? m_store.albumsNeedingDownload() : QVector<AlbumRecord>{};
    if (remixes.isEmpty() && albums.isEmpty()) {
        QMessageBox::information(this, tr("Downloads"),
                                 tr("Everything in the catalog is already downloaded, covered by an official torrent, or unavailable."));
        return;
    }
    m_queueTable->setRowCount(0);
    const QString library = Settings::instance().libraryPath();
    const int delay = Settings::instance().requestDelayMs();
    const bool preferTorrents = Settings::instance().preferTorrents();
    const bool includeAlbums = Settings::instance().includeAlbums();
    const bool downloadInApp = Settings::instance().downloadOfficialTorrentsInApp();
    const int concurrency = Settings::instance().concurrency();
    const bool openTorrents = Settings::instance().openTorrents();
    m_tabs->setCurrentIndex(2);
    QMetaObject::invokeMethod(m_downloads, [this, remixes, albums, library, delay, preferTorrents, includeAlbums, downloadInApp, openTorrents, concurrency]() {
        m_downloads->setLibraryPath(library);
        m_downloads->setDelayMs(delay);
        m_downloads->setConcurrency(concurrency);
        m_downloads->setPreferTorrents(preferTorrents);
        m_downloads->setIncludeAlbums(includeAlbums);
        m_downloads->setDownloadInApp(downloadInApp);
        m_downloads->setOpenTorrents(openTorrents);
        m_downloads->enqueueRemixes(remixes);
        m_downloads->enqueueAlbums(albums);
        m_downloads->start();
    }, Qt::QueuedConnection);
}
