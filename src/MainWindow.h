#pragma once

#include "CatalogStore.h"
#include "Types.h"

#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QThread>

class QLabel;
class QLineEdit;
class QProgressBar;
class QTableWidget;
class QTextEdit;
class QTabWidget;
class CatalogSync;
class DownloadQueue;

#ifdef OCRCATALOG_HAS_MULTIMEDIA
class QMediaPlayer;
class QAudioOutput;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool ready() { return m_store.open(); }
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

private:
    void buildUi();
    void reloadTables();
    void fillRemixTable();
    void fillAlbumTable();
    void updateStatus();
    void ensureLibrary();
    QVector<int> selectedRemixRows() const;
    QVector<int> selectedAlbumRows() const;

    CatalogStore m_store;
    QThread m_worker;
    CatalogSync *m_sync = nullptr;
    DownloadQueue *m_downloads = nullptr;

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
