#include "OfficialTorrentEngine.h"

#include "OfficialTorrentPolicy.h"

#include <QFileInfo>
#include <QTimer>

#ifdef OCRCATALOG_HAS_TORRENT
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <stdexcept>
namespace lt = libtorrent;
#endif

OfficialTorrentEngine::OfficialTorrentEngine(QObject *parent)
    : QObject(parent)
{
}

OfficialTorrentEngine::~OfficialTorrentEngine() = default;

bool OfficialTorrentEngine::available()
{
#ifdef OCRCATALOG_HAS_TORRENT
    return true;
#else
    return false;
#endif
}

void OfficialTorrentEngine::start()
{
#ifdef OCRCATALOG_HAS_TORRENT
    if (m_session)
        return;
    lt::settings_pack pack;
    pack.set_str(lt::settings_pack::user_agent, "OCRCatalog/1.0");
    pack.set_int(lt::settings_pack::alert_mask,
                 lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage);
    pack.set_bool(lt::settings_pack::enable_dht, true);
    pack.set_bool(lt::settings_pack::enable_lsd, true);
    pack.set_bool(lt::settings_pack::enable_upnp, true);
    pack.set_bool(lt::settings_pack::enable_natpmp, true);
    pack.set_int(lt::settings_pack::active_downloads, 4);
    pack.set_int(lt::settings_pack::active_seeds, 4);
    pack.set_int(lt::settings_pack::active_limit, 8);
    m_session = std::make_unique<lt::session>(pack);
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &OfficialTorrentEngine::poll);
    m_timer->start();
#endif
}

bool OfficialTorrentEngine::addOfficialTorrent(const QString &sourceUrl, const QString &torrentFile,
                                               const QString &savePath)
{
#ifndef OCRCATALOG_HAS_TORRENT
    Q_UNUSED(sourceUrl);
    Q_UNUSED(torrentFile);
    Q_UNUSED(savePath);
    return false;
#else
    if (!m_session)
        start();
    if (!OfficialTorrentPolicy::isOfficialTorrentUrl(sourceUrl)) {
        emit torrentFailed(QFileInfo(torrentFile).fileName(),
                           tr("Only official OC ReMix torrents from bt.ocremix.org can be added."));
        return false;
    }
    if (!QFileInfo::exists(torrentFile) || savePath.isEmpty()) {
        emit torrentFailed(QFileInfo(torrentFile).fileName(), tr("Missing torrent file or save folder."));
        return false;
    }

    std::shared_ptr<lt::torrent_info> info;
    try {
        info = std::make_shared<lt::torrent_info>(torrentFile.toStdString());
    } catch (const std::exception &ex) {
        emit torrentFailed(QFileInfo(torrentFile).fileName(), QString::fromUtf8(ex.what()));
        return false;
    }

    bool officialTracker = false;
    for (const auto &tracker : info->trackers()) {
        if (OfficialTorrentPolicy::isOfficialTrackerUrl(QString::fromStdString(tracker.url))) {
            officialTracker = true;
            break;
        }
    }
    if (!officialTracker) {
        emit torrentFailed(QFileInfo(torrentFile).fileName(),
                           tr("Rejected: torrent does not use an OC ReMix tracker."));
        return false;
    }

    lt::add_torrent_params params;
    params.ti = std::move(info);
    params.save_path = savePath.toStdString();
    const QString name = QString::fromStdString(params.ti->name());
    try {
        m_session->add_torrent(std::move(params));
    } catch (const std::exception &ex) {
        emit torrentFailed(name, QString::fromUtf8(ex.what()));
        return false;
    }

    DownloadJob job;
    job.kind = DownloadJob::CollectionTorrent;
    job.itemId = QStringLiteral("torrent:") + name;
    job.displayName = name;
    job.status = QStringLiteral("running");
    emit jobUpdated(job);
    return true;
#endif
}

void OfficialTorrentEngine::poll()
{
#ifdef OCRCATALOG_HAS_TORRENT
    if (!m_session)
        return;
    m_session->post_torrent_updates();
    std::vector<lt::alert *> alerts;
    m_session->pop_alerts(&alerts);
    for (lt::alert *alert : alerts) {
        if (auto *update = lt::alert_cast<lt::state_update_alert>(alert)) {
            for (const auto &st : update->status) {
                DownloadJob job;
                job.kind = DownloadJob::CollectionTorrent;
                job.itemId = QStringLiteral("torrent:") + QString::fromStdString(st.name);
                job.displayName = QString::fromStdString(st.name);
                job.bytesDone = st.total_wanted_done;
                job.bytesTotal = st.total_wanted;
                if (st.is_finished)
                    job.status = QStringLiteral("done");
                else if (st.errc)
                    job.status = QStringLiteral("error");
                else
                    job.status = QStringLiteral("running");
                if (st.errc)
                    job.error = QString::fromStdString(st.errc.message());
                emit jobUpdated(job);
            }
        } else if (auto *finished = lt::alert_cast<lt::torrent_finished_alert>(alert)) {
            const auto st = finished->handle.status();
            DownloadJob job;
            job.kind = DownloadJob::CollectionTorrent;
            job.itemId = QStringLiteral("torrent:") + QString::fromStdString(st.name);
            job.displayName = QString::fromStdString(st.name);
            job.status = QStringLiteral("done");
            job.bytesDone = st.total_wanted_done;
            job.bytesTotal = st.total_wanted;
            emit jobUpdated(job);
        } else if (auto *err = lt::alert_cast<lt::torrent_error_alert>(alert)) {
            emit torrentFailed(QString::fromStdString(err->torrent_name()),
                               QString::fromStdString(err->message()));
        }
    }
#endif
}
