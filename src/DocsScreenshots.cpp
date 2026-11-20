#include "MainWindow.h"

#include "OfficialTorrentEngine.h"
#include "Settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

void MainWindow::openSettingsDialog(const QString &screenshotPath)
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
    auto *note = new QLabel(tr("Catalog downloads start with the two official packs "
                              "(OCR 1–4000 and 4001–4473). HTTPS mirrors are used for newer tracks "
                              "and for anything the in-app torrent engine cannot take. "
                              "Only bt.ocremix.org torrents are accepted."));
    note->setWordWrap(true);
    form->addRow(tr("Note"), note);
    form->addRow(torrents);
    form->addRow(inAppTorrents);
    form->addRow(openTorrents);
    form->addRow(albums);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (!screenshotPath.isEmpty()) {
        dlg.setFixedWidth(620);
        dlg.adjustSize();
        dlg.show();
        QApplication::processEvents();
        dlg.repaint();
        QApplication::processEvents();
        if (!dlg.grab().save(screenshotPath, "PNG"))
            qWarning() << "Could not write settings screenshot" << screenshotPath;
        dlg.close();
        return;
    }
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

int MainWindow::writeDocsScreenshots(const QString &outputDir)
{
    if (!QDir().mkpath(outputDir)) {
        qWarning() << "Cannot create screenshot folder" << outputDir;
        return 1;
    }
    Settings::instance().setMenuInstallPrompted(true);

    auto flush = []() {
        QApplication::processEvents();
        for (QWidget *w : QApplication::topLevelWidgets())
            w->repaint();
        QApplication::processEvents();
    };
    auto save = [&](const QString &name, QWidget *widget) -> bool {
        flush();
        const QString path = outputDir + QLatin1Char('/') + name;
        const QPixmap pix = widget->grab();
        if (pix.isNull() || !pix.save(path, "PNG")) {
            qWarning() << "Failed to write" << path;
            return false;
        }
        qInfo().noquote() << "Wrote" << path << QStringLiteral("%1x%2").arg(pix.width()).arg(pix.height());
        return true;
    };

    show();
    resize(1280, 800);
    Settings::instance().setLibraryPath({});
    updateStatus();
    flush();
    if (!save(QStringLiteral("empty.png"), this))
        return 1;

    Settings::instance().setLibraryPath(QStringLiteral("/home/matt/Music/OC ReMix"));
    Settings::instance().setPreferTorrents(true);
    Settings::instance().setDownloadOfficialTorrentsInApp(true);
    Settings::instance().setIncludeAlbums(true);

    QVector<RemixRecord> remixes;
    auto addRemix = [&](const char *id, int number, const char *title, const char *game,
                        const char *artists, const char *posted, const char *status,
                        const char *duration) {
        RemixRecord r;
        r.ocrId = QString::fromLatin1(id);
        r.ocrNumber = number;
        r.title = QString::fromUtf8(title);
        r.game = QString::fromUtf8(game);
        r.artists = QString::fromUtf8(artists);
        r.posted = QString::fromLatin1(posted);
        r.status = QString::fromLatin1(status);
        r.duration = QString::fromLatin1(duration);
        r.pageUrl = QStringLiteral("https://ocremix.org/remix/") + r.ocrId;
        remixes.push_back(r);
    };
    // Public OC ReMix catalog rows (titles, IDs, and artists from ocremix.org).
    addRemix("OCR05089", 5089, "Psy Want It That Way", "Pokémon Blue Version",
             "Emunator, Biggoron, Ian Cowell, ZackParrish", "NOV 15 2026", "available", "4:41");
    addRemix("OCR05088", 5088, "Lunar Apocalypse", "Kingdom Hearts II",
             "Laura Platt, Katy Scary, Shea's Violin", "NOV 03 2026", "available", "5:18");
    addRemix("OCR05086", 5086, "The Madness Machine", "Chrono Trigger", "Argle",
             "OCT 31 2026", "available", "4:02");
    addRemix("OCR05083", 5083, "Sanguine Sanctum", "Bloodborne", "Fishy, Cyril the Wolf",
             "OCT 08 2026", "available", "6:11");
    addRemix("OCR05075", 5075, "Of Souls and Shadows", "Undertale", "jnWake",
             "SEP 12 2026", "available", "5:44");
    addRemix("OCR05072", 5072, "Azurite Horizons", "Donkey Kong Country", "Rom Tom",
             "AUG 13 2026", "available", "4:55");
    addRemix("OCR01372", 1372, "Shattered", "Shatterhand", "Rubbler, SnappleMan",
             "JUN 22 2008", "downloaded", "4:12");
    addRemix("OCR00001", 1, "Shin Shuriken Jam", "Shinobi", "djpretzel",
             "DEC 15 1999", "in-torrent", "3:21");
    remixes[0].mirrors = QStringList{
        QStringLiteral("https://ocremix.org/remix/OCR05089")};
    m_store.upsertRemixes(remixes);

    QVector<AlbumRecord> albums;
    auto addAlbum = [&](int id, const char *title, const char *catalog, const char *released,
                        const char *classification, const char *status, const char *slug) {
        AlbumRecord a;
        a.albumId = id;
        a.title = QString::fromUtf8(title);
        a.catalog = QString::fromLatin1(catalog);
        a.released = QString::fromLatin1(released);
        a.classification = QString::fromUtf8(classification);
        a.status = QString::fromLatin1(status);
        a.slug = QString::fromLatin1(slug);
        a.ocrPublished = true;
        a.pageUrl = QStringLiteral("https://ocremix.org/album/%1/%2").arg(id).arg(a.slug);
        albums.push_back(a);
    };
    addAlbum(1, "Super Metroid: Relics of the Chozo", "OCRA-0001", "SEP 12 2003",
             "OC ReMix Album", "downloaded", "super-metroid-relics-of-the-chozo");
    addAlbum(26, "Final Fantasy VI: Balance and Ruin", "OCRA-0026", "JUL 01 2013",
             "OC ReMix Album", "in-torrent", "final-fantasy-vi-balance-and-ruin");
    addAlbum(60069, "Lufia II: Of Gods and Men", "OCRA-0082", "JUN 30 2026",
             "OC ReMix Album", "available", "lufia-ii-of-gods-and-men");
    addAlbum(60067, "TimeShift: The OverClocked ReMix 25th Anniversary Album", "OCRA-0080",
             "DEC 10 2025", "OC ReMix Album", "available",
             "timeshift-the-overclocked-remix-25th-anniversary-album");
    addAlbum(60070, "Fall & Rise", "OCRI-0016", "AUG 28 2026",
             "OC ReMix Album", "available", "fall-and-rise");
    m_store.upsertAlbums(albums);

    reloadTables();
    updateStatus();
    m_progress->setRange(0, 100);
    m_progress->setValue(38);
    m_statusLabel->setText(
        tr("Official torrents cover through OCR04473. Fetching newer ReMixes from HTTPS mirrors."));
    m_remixTable->resizeColumnsToContents();
    m_remixTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_remixTable->selectRow(0);
    m_tabs->setCurrentIndex(0);
    if (auto *split = findChild<QSplitter *>())
        split->setSizes({820, 420});
    if (!save(QStringLiteral("remixes.png"), this))
        return 1;

    m_tabs->setCurrentIndex(1);
    m_albumTable->resizeColumnsToContents();
    m_albumTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_albumTable->selectRow(0);
    if (!save(QStringLiteral("albums.png"), this))
        return 1;

    DownloadJob collection;
    collection.kind = DownloadJob::CollectionTorrent;
    collection.itemId = QStringLiteral("torrent:OCR 1-4000");
    collection.displayName = QStringLiteral("OC ReMix Collection 1 to 4000");
    collection.status = QStringLiteral("downloading");
    collection.bytesDone = 18LL * 1048576 * 120;
    collection.bytesTotal = 18LL * 1048576 * 400;
    applyJobToQueueTable(collection);
    DownloadJob https;
    https.kind = DownloadJob::Remix;
    https.itemId = QStringLiteral("OCR05089");
    https.displayName = QStringLiteral("OCR05089 Psy Want It That Way");
    https.status = QStringLiteral("queued");
    applyJobToQueueTable(https);
    DownloadJob done;
    done.kind = DownloadJob::AlbumZip;
    done.itemId = QStringLiteral("album:1");
    done.displayName = QStringLiteral("OCRA-0001 Super Metroid: Relics of the Chozo");
    done.status = QStringLiteral("done");
    done.bytesDone = done.bytesTotal = 180 * 1048576;
    applyJobToQueueTable(done);
    m_tabs->setCurrentIndex(2);
    m_queueTable->resizeColumnsToContents();
    m_queueTable->horizontalHeader()->setStretchLastSection(true);
    if (!save(QStringLiteral("queue.png"), this))
        return 1;

    m_tabs->setCurrentIndex(0);
    openSettingsDialog(outputDir + QStringLiteral("/settings.png"));
    if (!QFileInfo::exists(outputDir + QStringLiteral("/settings.png")))
        return 1;
    return 0;
}
