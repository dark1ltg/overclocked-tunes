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
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
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
#include <QItemSelectionModel>
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
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { shutdownWorkers(); });
}

void MainWindow::shutdownWorkers()
{
    if (!m_workersStarted && !m_sync && !m_downloads && !m_torrentEngine)
        return;
    if (m_sync)
        m_sync->cancel();
    if (m_downloads)
        m_downloads->cancel();
    if (m_worker.isRunning()) {
        if (m_downloads)
            QMetaObject::invokeMethod(m_downloads, "cancelAndAbort", Qt::QueuedConnection);
        if (m_torrentEngine)
            QMetaObject::invokeMethod(m_torrentEngine, "stop", Qt::BlockingQueuedConnection);
        m_worker.quit();
        if (!m_worker.wait(20000)) {
            qWarning() << "OCR Catalog worker did not exit; stopping torrents on the UI thread.";
            if (m_torrentEngine) {
                m_torrentEngine->moveToThread(QThread::currentThread());
                m_torrentEngine->stop();
            }
            m_worker.quit();
            m_worker.wait(5000);
        }
    } else if (m_torrentEngine) {
        m_torrentEngine->stop();
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    shutdownWorkers();
    QMainWindow::closeEvent(event);
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
    tool->addAction(tr("Play / Resume"), this, &MainWindow::playSelected);
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
    editMenu->addAction(tr("Play / Resume"), this, &MainWindow::playSelected);
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
                              "(OCR 1–4000 and 4001–4473) and waits until both finish, then downloads "
                              "album torrents one at a time, then leftover tracks one at a time. "
                              "Tracks already pulled from those torrents are skipped. Closing the app "
                              "stops in-app torrents and HTTPS downloads so nothing keeps using the network, "
                              "and saved progress is kept for the next launch.</p>"
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
    m_albumTable->setHorizontalHeaderLabels({tr("Catalog"), tr("Title"), tr("Classification"), tr("Released"), tr("Downloadable")});
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
    m_queueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_queueTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_queueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_queueTable->setAlternatingRowColors(true);
    m_queueTable->horizontalHeader()->setStretchLastSection(true);
    m_queueTable->verticalHeader()->setVisible(false);
    connect(m_queueTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) {
        playSelected();
    });

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
           "(OCR 1–4000 and 4001–4473), then album torrents one by one, then leftover tracks."));
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
