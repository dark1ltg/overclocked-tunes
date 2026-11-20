#pragma once

#include "CatalogStore.h"
#include "JobGate.h"
#include "Types.h"

#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QThread>

class QLabel;
class QLineEdit;
class QProgressBar;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;
class QTabWidget;
class CatalogSync;
class DownloadQueue;
class OfficialTorrentEngine;

#ifdef OCRCATALOG_HAS_MULTIMEDIA
class QMediaPlayer;
class QAudioOutput;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool ready();
    QString storeError() const { return m_store.lastError(); }

private slots:
    void chooseLibrary();
    void syncCatalog();
    void downloadMissing();
    void downloadSelected();
    void pauseDownloads();
    void openSettings();
    void installMenu();
    void removeMenu();
    void playSelected();
    void onSearchChanged(const QString &text);
    void onRemixSelectionChanged();
    void onAlbumSelectionChanged();
    void onRemixesParsed(const QVector<RemixRecord> &records);
    void onAlbumsParsed(const QVector<AlbumRecord> &records);
    void onSyncFinished(bool ok, const QString &message);
    void onDownloadFinished(bool ok, const QString &message);
    void maybeOfferMenuInstall();
    void onRemixRangeCovered(int from, int to, const QString &note);
    void onTorrentsReady(const QStringList &paths);
    void onCoverageSummary(int lastOcr, int collectionTracks, int remainingRemixes, int remainingAlbums);
    void onTorrentCompleted(const QString &name, const QString &savePath);

private:
    void buildUi();
    void setupWorkers();
    void shutdownWorkers();
    void reloadTables();
    void fillRemixTable();
    void fillAlbumTable();
    void applyRemixToTable(const RemixRecord &record);
    void applyAlbumToTable(const AlbumRecord &record);
    void updateStatus();
    void ensureLibrary();
    void applyJobToQueueTable(const DownloadJob &job);
    QVector<int> selectedRemixRows() const;
    QVector<int> selectedAlbumRows() const;
    RemixRecord remixAtRow(int row) const;
    AlbumRecord albumAtRow(int row) const;
    QTableWidgetItem *taggedItem(const QString &text, const QString &id) const;

    CatalogStore m_store;
    JobGate m_jobGate;
    QThread m_worker;
    CatalogSync *m_sync = nullptr;
    DownloadQueue *m_downloads = nullptr;
    OfficialTorrentEngine *m_torrentEngine = nullptr;
    bool m_workersStarted = false;

    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_remixTable = nullptr;
    QTableWidget *m_albumTable = nullptr;
    QTableWidget *m_queueTable = nullptr;
    QTextEdit *m_detail = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QVector<RemixRecord> m_remixCache;
    QVector<AlbumRecord> m_albumCache;

#ifdef OCRCATALOG_HAS_MULTIMEDIA
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
#endif
};
