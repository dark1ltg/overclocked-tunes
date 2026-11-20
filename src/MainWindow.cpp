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
                                        ? tr("Official torrents are downloaded inside this app. "
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

void MainWindow::downloadSelected()
{
    if ((m_downloads && m_downloads->isRunning()) || (m_sync && m_sync->isRunning()) || m_jobGate.locked()) {
        QMessageBox::information(this, tr("Downloads"),
                                 tr("Wait for the current sync or download to finish (or cancel it) first."));
        return;
    }
    ensureLibrary();
    if (Settings::instance().libraryPath().isEmpty())
        return;
    QVector<RemixRecord> remixes;
    QVector<AlbumRecord> albums;
    if (m_tabs->currentIndex() == 0) {
        for (int row : selectedRemixRows())
            remixes.push_back(remixAtRow(row));
    } else if (m_tabs->currentIndex() == 1) {
        for (int row : selectedAlbumRows())
            albums.push_back(albumAtRow(row));
    }
    if (remixes.isEmpty() && albums.isEmpty()) {
        QMessageBox::information(this, tr("Downloads"), tr("Select one or more ReMixes or albums first."));
        return;
    }
    const QString library = Settings::instance().libraryPath();
    const int delay = Settings::instance().requestDelayMs();
    const int concurrency = Settings::instance().concurrency();
    m_tabs->setCurrentIndex(2);
    QMetaObject::invokeMethod(m_downloads, [this, remixes, albums, library, delay, concurrency]() {
        m_downloads->setLibraryPath(library);
        m_downloads->setDelayMs(delay);
        m_downloads->setConcurrency(concurrency);
        m_downloads->setPreferTorrents(false);
        m_downloads->setDownloadInApp(false);
        m_downloads->setIncludeAlbums(true);
        m_downloads->enqueueRemixes(remixes);
        m_downloads->enqueueAlbums(albums);
        m_downloads->start();
    }, Qt::QueuedConnection);
}

void MainWindow::pauseDownloads()
{
    if (m_sync)
        m_sync->cancel();
    if (m_downloads)
        m_downloads->cancel();
    if (m_torrentEngine)
        QMetaObject::invokeMethod(m_torrentEngine, "cancelActive", Qt::QueuedConnection);
    m_statusLabel->setText(tr("Cancelling…"));
}

void MainWindow::openSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    auto *form = new QFormLayout(&dlg);
    auto *lib = new QLineEdit(Settings::instance().libraryPath());
    auto *browse = new QPushButton(tr("Browse…"));
    auto *libRow = new QWidget;
    auto *libLayout = new QHBoxLayout(libRow);
    libLayout->setContentsMargins(0, 0, 0, 0);
    libLayout->addWidget(lib, 1);
    libLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dlg, [this, lib]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Library folder"), lib->text());
        if (!dir.isEmpty())
            lib->setText(dir);
    });
    auto *conc = new QSpinBox;
    conc->setRange(1, 3);
    conc->setValue(Settings::instance().concurrency());
    auto *delay = new QSpinBox;
    delay->setRange(250, 2000);
    delay->setSuffix(tr(" ms"));
    delay->setValue(Settings::instance().requestDelayMs());
    auto *albums = new QCheckBox(tr("Include official OC ReMix albums when downloading missing items"));
    albums->setChecked(Settings::instance().includeAlbums());
    auto *torrents = new QCheckBox(tr("Use official torrents first for the back-catalog (recommended)"));
    torrents->setChecked(Settings::instance().preferTorrents());
    auto *inAppTorrents = new QCheckBox(tr("Download official OC ReMix torrents inside this app"));
    inAppTorrents->setChecked(Settings::instance().downloadOfficialTorrentsInApp());
    if (!OfficialTorrentEngine::available()) {
        inAppTorrents->setChecked(false);
        inAppTorrents->setEnabled(false);
        inAppTorrents->setToolTip(tr("This build was compiled without libtorrent. Save the official "
                                     ".torrent files and open them in a BitTorrent client."));
    }
    auto *openTorrents = new QCheckBox(tr("Also open collection torrents in the system BitTorrent client"));
    openTorrents->setChecked(Settings::instance().openTorrents());
    form->addRow(tr("Library"), libRow);
    form->addRow(tr("HTTPS connections"), conc);
    form->addRow(tr("Polite delay"), delay);
    form->addRow(tr("Note"), new QLabel(tr("Catalog downloads start with the two official packs "
                                           "(OCR 1–4000 and 4001–4473). HTTPS mirrors are used for newer tracks "
                                           "and for anything the in-app torrent engine cannot take. "
                                           "Only bt.ocremix.org torrents are accepted.")));
    form->addRow(torrents);
    form->addRow(inAppTorrents);
    form->addRow(openTorrents);
    form->addRow(albums);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString newLib = lib->text().trimmed();
    Settings::instance().setLibraryPath(newLib);
    Settings::instance().setConcurrency(conc->value());
    Settings::instance().setRequestDelayMs(delay->value());
    Settings::instance().setIncludeAlbums(albums->isChecked());
    Settings::instance().setPreferTorrents(torrents->isChecked());
    Settings::instance().setDownloadOfficialTorrentsInApp(inAppTorrents->isChecked());
    Settings::instance().setOpenTorrents(openTorrents->isChecked());
    if (!newLib.isEmpty()) {
        QDir().mkpath(newLib + QStringLiteral("/remixes"));
        QDir().mkpath(newLib + QStringLiteral("/albums"));
        QDir().mkpath(newLib + QStringLiteral("/torrents"));
        QDir().mkpath(newLib + QStringLiteral("/collections"));
        m_store.markExistingFiles(newLib);
        reloadTables();
    }
    updateStatus();
}

void MainWindow::installMenu()
{
    QString error;
    if (!DesktopIntegrator::install(&error)) {
        QMessageBox::warning(this, tr("Application menu"), error);
        return;
    }
    Settings::instance().setMenuInstalled(true);
    QMessageBox::information(this, tr("Application menu"),
                             tr("OCR Catalog was added to your application menu "
                                "(KDE Plasma, GNOME, and other Freedesktop desktops).\n\n"
                                "If it does not appear immediately, log out or run:\n"
                                "update-desktop-database ~/.local/share/applications"));
}

void MainWindow::removeMenu()
{
    QString error;
    DesktopIntegrator::uninstall(&error);
    Settings::instance().setMenuInstalled(false);
    QMessageBox::information(this, tr("Application menu"), tr("Removed OCR Catalog from the application menu."));
}

void MainWindow::playSelected()
{
    if (m_tabs->currentIndex() == 1) {
        const auto rows = selectedAlbumRows();
        if (rows.isEmpty()) {
            QMessageBox::information(this, tr("Play"), tr("Select an album first."));
            return;
        }
        AlbumRecord record = albumAtRow(rows.first());
        if (record.localPath.isEmpty() || !QFileInfo::exists(record.localPath)) {
            m_store.markExistingFiles(Settings::instance().libraryPath(), false);
            record = m_store.album(record.albumId);
            applyAlbumToTable(record);
        }
        if (record.localPath.isEmpty() || !QFileInfo::exists(record.localPath)
            || record.localPath.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive)) {
            QMessageBox::information(this, tr("Play"),
                                     tr("Download this album first, then Play will open the zip or folder."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(record.localPath));
        m_statusLabel->setText(tr("Opened %1").arg(record.title));
        return;
    }
    if (m_tabs->currentIndex() != 0) {
        QMessageBox::information(this, tr("Play"), tr("Select a ReMix or album first."));
        return;
    }
    const auto rows = selectedRemixRows();
    if (rows.isEmpty())
        return;
    RemixRecord record = remixAtRow(rows.first());
    if (record.localPath.isEmpty() || !QFileInfo::exists(record.localPath)) {
        m_store.markExistingFiles(Settings::instance().libraryPath(), false);
        record = m_store.remix(record.ocrId);
        applyRemixToTable(record);
    }
    if (record.localPath.isEmpty() || !QFileInfo::exists(record.localPath)) {
        QMessageBox::information(this, tr("Play"), tr("Download this ReMix first."));
        return;
    }
#ifdef OCRCATALOG_HAS_MULTIMEDIA
    m_player->setSource(QUrl::fromLocalFile(record.localPath));
    m_player->play();
    m_statusLabel->setText(tr("Playing %1").arg(record.title));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(record.localPath));
#endif
}

void MainWindow::onSearchChanged(const QString &)
{
    reloadTables();
}

void MainWindow::onRemixSelectionChanged()
{
    const auto rows = selectedRemixRows();
    if (rows.isEmpty())
        return;
    const auto r = remixAtRow(rows.first());
    m_detail->setHtml(QStringLiteral(
                          "<h2>%1</h2><p><b>%2</b></p><p>%3</p><p>Posted %4 · %5</p>"
                          "<p><a href=\"%6\">Open on OC ReMix</a></p><p>%7</p><pre>%8</pre>")
                          .arg(r.title.toHtmlEscaped(), r.ocrId, r.game.toHtmlEscaped(), r.posted,
                               r.duration, r.pageUrl, r.artists.toHtmlEscaped(),
                               r.mirrors.join(QLatin1Char('\n')).toHtmlEscaped()));
}

void MainWindow::onAlbumSelectionChanged()
{
    const auto rows = selectedAlbumRows();
    if (rows.isEmpty())
        return;
    const auto a = albumAtRow(rows.first());
    m_detail->setHtml(QStringLiteral(
                          "<h2>%1</h2><p>%2 · %3</p><p>%4</p><p>%5</p>"
                          "<p><a href=\"%6\">Album page</a></p>%7%8")
                          .arg(a.title.toHtmlEscaped(), a.catalog, a.released, a.classification,
                               a.ocrPublished ? tr("Official OC ReMix album") : tr("Listed only (not downloaded)"),
                               a.pageUrl,
                               a.httpHomepage.isEmpty()
                                   ? QString()
                                   : QStringLiteral("<p><a href=\"%1\">HTTP / album site</a></p>").arg(a.httpHomepage),
                               a.torrentUrl.isEmpty()
                                   ? QString()
                                   : QStringLiteral("<p><a href=\"%1\">BitTorrent</a></p>").arg(a.torrentUrl)));
}

void MainWindow::onRemixesParsed(const QVector<RemixRecord> &records)
{
    m_store.upsertRemixes(records);
    m_tabs->setTabText(0, tr("ReMixes (%1)").arg(m_store.remixCount()));
    if (auto *hint = findChild<QLabel *>(QStringLiteral("emptyHint")))
        hint->setVisible(false);
}

void MainWindow::onAlbumsParsed(const QVector<AlbumRecord> &records)
{
    m_store.upsertAlbums(records);
    m_tabs->setTabText(1, tr("Albums (%1)").arg(m_store.albumCount()));
}

void MainWindow::onSyncFinished(bool ok, const QString &message)
{
    m_progress->setRange(0, 1);
    m_progress->setValue(1);
    if (!ok)
        QMessageBox::warning(this, tr("Sync"), message);
    m_store.markExistingFiles(Settings::instance().libraryPath());
    reloadTables();
    updateStatus();
    m_statusLabel->setText(message);
}

void MainWindow::onDownloadFinished(bool ok, const QString &message)
{
    Q_UNUSED(ok);
    m_progress->setRange(0, 1);
    m_progress->setValue(1);
    m_store.markExistingFiles(Settings::instance().libraryPath());
    reloadTables();
    updateStatus();
    m_statusLabel->setText(message);
}

void MainWindow::onRemixRangeCovered(int from, int to, const QString &note)
{
    m_store.markRemixesInTorrentRange(from, to, note);
    m_store.setMeta(QStringLiteral("torrentLastOcr"), QString::number(to));
    updateStatus();
}

void MainWindow::onTorrentCompleted(const QString &name, const QString &savePath)
{
    Q_UNUSED(name);
    Q_UNUSED(savePath);
    m_store.markExistingFiles(Settings::instance().libraryPath());
    reloadTables();
    updateStatus();
    m_statusLabel->setText(tr("Official collection torrent finished. Matching files are now playable."));
}

void MainWindow::onTorrentsReady(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    if (Settings::instance().downloadOfficialTorrentsInApp() && OfficialTorrentEngine::available())
        return;
    if (!Settings::instance().openTorrents())
        return;
    for (const auto &path : paths)
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::onCoverageSummary(int lastOcr, int collectionTracks, int remainingRemixes, int remainingAlbums)
{
    m_store.setMeta(QStringLiteral("torrentLastOcr"), QString::number(lastOcr));
    m_store.setMeta(QStringLiteral("torrentTrackCount"), QString::number(collectionTracks));
    if (lastOcr <= 0) {
        m_statusLabel->setText(tr("Official collection torrents were not saved. "
                                  "Fetching remaining items from HTTPS mirrors."));
        return;
    }
    m_statusLabel->setText(tr("Official torrents cover through OCR%1 (%2 tracks). "
                              "Fetching %3 newer ReMixes and %4 albums not in those torrents.")
                               .arg(lastOcr, 5, 10, QLatin1Char('0'))
                               .arg(collectionTracks)
                               .arg(remainingRemixes)
                               .arg(remainingAlbums));
}

void MainWindow::maybeOfferMenuInstall()
{
    if (!DesktopIntegrator::isRunningAsAppImage())
        return;
    if (Settings::instance().menuInstallPrompted() || DesktopIntegrator::isInstalled())
        return;
    Settings::instance().setMenuInstallPrompted(true);
    const auto answer = QMessageBox::question(
        this, tr("Application menu"),
        tr("Add OCR Catalog to your application menu so it appears in KDE Plasma, GNOME, and other desktops?"));
    if (answer == QMessageBox::Yes)
        installMenu();
}

QVector<int> MainWindow::selectedRemixRows() const
{
    QVector<int> rows;
    for (auto *item : m_remixTable->selectedItems()) {
        if (item->column() == 0 && !rows.contains(item->row()))
            rows.push_back(item->row());
    }
    return rows;
}

QVector<int> MainWindow::selectedAlbumRows() const
{
    QVector<int> rows;
    for (auto *item : m_albumTable->selectedItems()) {
        if (item->column() == 0 && !rows.contains(item->row()))
            rows.push_back(item->row());
    }
    return rows;
}
