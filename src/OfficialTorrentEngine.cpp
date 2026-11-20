#include "OfficialTorrentEngine.h"

#include "OfficialTorrentPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>

#ifdef OCRCATALOG_HAS_TORRENT
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <stdexcept>
namespace lt = libtorrent;

static QString hashToHex(const lt::info_hash_t &hashes)
{
    const lt::sha1_hash best = hashes.get_best();
    return QString::fromLatin1(QByteArray(best.data(), static_cast<int>(lt::sha1_hash::size())).toHex());
}

static QString bdecodeString(const lt::bdecode_node &node)
{
    if (node.type() != lt::bdecode_node::string_t)
        return {};
    return QString::fromUtf8(node.string_ptr(), node.string_length());
}

// torrent_info::trackers() is hidden on libtorrent 2.1 with TORRENT_ABI_VERSION 100
// (vcpkg default). Read announce / announce-list from the .torrent instead.
static bool torrentFileHasOfficialTracker(const QString &torrentFile)
{
    QFile file(torrentFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty())
        return false;

    lt::error_code ec;
    lt::bdecode_node root;
    const int rc = lt::bdecode(bytes.constData(), bytes.constData() + bytes.size(), root, ec);
    if (rc != 0 || ec || root.type() != lt::bdecode_node::dict_t)
        return false;

    if (OfficialTorrentPolicy::isOfficialTrackerUrl(bdecodeString(root.dict_find("announce"))))
        return true;

    const lt::bdecode_node list = root.dict_find_list("announce-list");
    if (list.type() != lt::bdecode_node::list_t)
        return false;
    for (int i = 0; i < list.list_size(); ++i) {
        const lt::bdecode_node tier = list.list_at(i);
        if (tier.type() != lt::bdecode_node::list_t)
            continue;
        for (int j = 0; j < tier.list_size(); ++j) {
            if (OfficialTorrentPolicy::isOfficialTrackerUrl(bdecodeString(tier.list_at(j))))
                return true;
        }
    }
    return false;
}
#endif

OfficialTorrentEngine::OfficialTorrentEngine(QObject *parent)
    : QObject(parent)
{
}

OfficialTorrentEngine::~OfficialTorrentEngine()
{
#ifdef OCRCATALOG_HAS_TORRENT
    if (m_session)
        stop();
#endif
}

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
    if (m_stopped || m_session)
        return;
    try {
        lt::settings_pack pack;
        pack.set_str(lt::settings_pack::user_agent, "OCRCatalog/1.0.1");
        pack.set_int(lt::settings_pack::alert_mask,
                     lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage);
        pack.set_bool(lt::settings_pack::enable_dht, true);
        pack.set_bool(lt::settings_pack::enable_lsd, true);
        pack.set_bool(lt::settings_pack::enable_upnp, true);
        pack.set_bool(lt::settings_pack::enable_natpmp, true);
        pack.set_int(lt::settings_pack::active_downloads, 4);
        pack.set_int(lt::settings_pack::active_seeds, 0);
        pack.set_int(lt::settings_pack::active_limit, 8);
        m_session = std::make_unique<lt::session>(pack);
        if (!m_timer) {
            m_timer = new QTimer(this);
            m_timer->setInterval(1000);
            connect(m_timer, &QTimer::timeout, this, &OfficialTorrentEngine::poll);
        }
        m_timer->start();
    } catch (const std::exception &ex) {
        m_session.reset();
        emit torrentFailed(QStringLiteral("session"), QString::fromUtf8(ex.what()));
    } catch (...) {
        m_session.reset();
        emit torrentFailed(QStringLiteral("session"), tr("Could not start the official torrent engine."));
    }
#else
    Q_UNUSED(this);
#endif
}

#ifdef OCRCATALOG_HAS_TORRENT
QString OfficialTorrentEngine::resumePath(const QString &savePath, const QString &hashHex) const
{
    return savePath + QStringLiteral("/.resume/") + hashHex + QStringLiteral(".fastresume");
}

void OfficialTorrentEngine::writeResumeFile(const QString &savePath, const QString &hashHex,
                                            const std::vector<char> &buf)
{
    if (savePath.isEmpty() || hashHex.isEmpty() || buf.empty())
        return;
    const QString path = resumePath(savePath, hashHex);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(buf.data(), static_cast<qint64>(buf.size()));
}
#endif

void OfficialTorrentEngine::stop()
{
#ifdef OCRCATALOG_HAS_TORRENT
    m_stopped = true;
    if (m_timer)
        m_timer->stop();
    if (!m_session)
        return;
    try {
        m_session->pause();
        m_pendingResume = 0;
        for (const auto &handle : m_session->get_torrents()) {
            if (!handle.is_valid())
                continue;
            handle.pause();
            handle.save_resume_data(lt::torrent_handle::flush_disk_cache | lt::torrent_handle::save_info_dict);
            ++m_pendingResume;
        }
        QElapsedTimer timer;
        timer.start();
        while (m_pendingResume > 0 && timer.elapsed() < 8000) {
            poll();
            if (m_pendingResume > 0)
                QThread::msleep(50);
        }
    } catch (const std::exception &) {
    }
    m_session.reset();
#endif
}

void OfficialTorrentEngine::cancelActive()
{
#ifdef OCRCATALOG_HAS_TORRENT
    if (!m_session)
        return;
    try {
        for (const auto &handle : m_session->get_torrents()) {
            if (!handle.is_valid())
                continue;
            handle.pause();
            handle.save_resume_data(lt::torrent_handle::flush_disk_cache);
            DownloadJob job;
            job.kind = DownloadJob::CollectionTorrent;
            const auto st = handle.status();
            const QString name = QString::fromStdString(st.name);
            job.itemId = jobIdForName(name);
            job.displayName = name;
            job.status = QStringLiteral("cancelled");
            job.bytesDone = st.total_wanted_done;
            job.bytesTotal = st.total_wanted;
            emit jobUpdated(job);
        }
    } catch (const std::exception &ex) {
        emit torrentFailed(QStringLiteral("session"), QString::fromUtf8(ex.what()));
    }
#endif
}

bool OfficialTorrentEngine::hasActiveTorrents() const
{
#ifdef OCRCATALOG_HAS_TORRENT
    if (!m_session)
        return false;
    try {
        for (const auto &handle : m_session->get_torrents()) {
            if (!handle.is_valid())
                continue;
            const auto st = handle.status();
            if (st.is_finished)
                continue;
            if (st.flags & lt::torrent_flags::paused)
                continue;
            if (st.errc)
                continue;
            return true;
        }
    } catch (const std::exception &) {
        return false;
    }
#endif
    return false;
}

#ifdef OCRCATALOG_HAS_TORRENT
QString OfficialTorrentEngine::jobIdForName(const QString &name) const
{
    return m_jobIds.value(name, QStringLiteral("torrent:") + name);
}
#endif

bool OfficialTorrentEngine::addOfficialTorrent(const QString &sourceUrl, const QString &torrentFile,
                                               const QString &savePath, const QString &itemId)
{
#ifndef OCRCATALOG_HAS_TORRENT
    Q_UNUSED(sourceUrl);
    Q_UNUSED(torrentFile);
    Q_UNUSED(savePath);
    Q_UNUSED(itemId);
    return false;
#else
    if (m_stopped)
        return false;
    if (!m_session)
        start();
    if (!m_session) {
        emit torrentFailed(QFileInfo(torrentFile).fileName(), tr("Torrent engine is not running."));
        return false;
    }
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

    if (!torrentFileHasOfficialTracker(torrentFile)) {
        emit torrentFailed(QFileInfo(torrentFile).fileName(),
                           tr("Rejected: torrent does not use an OC ReMix tracker."));
        return false;
    }

    QDir().mkpath(savePath);
    const qint64 needed = static_cast<qint64>(info->total_size()) + 64 * 1024 * 1024;
    const QStorageInfo storage(savePath);
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < needed) {
        emit torrentFailed(QString::fromStdString(info->name()),
                           tr("Not enough disk space for this collection (%1 MB needed).")
                               .arg(needed / (1024 * 1024)));
        return false;
    }

    lt::add_torrent_params params;
    const QString hashHex = hashToHex(info->info_hashes());
    const QString resumeFile = resumePath(savePath, hashHex);
    if (QFileInfo::exists(resumeFile)) {
        QFile file(resumeFile);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            lt::error_code ec;
            params = lt::read_resume_data(lt::span<char const>(data.constData(), data.size()), ec);
            if (ec)
                params = lt::add_torrent_params();
        }
    }
    params.ti = std::move(info);
    params.save_path = savePath.toStdString();
    const QString name = QString::fromStdString(params.ti->name());
    try {
        m_session->add_torrent(std::move(params));
    } catch (const std::exception &ex) {
        emit torrentFailed(name, QString::fromUtf8(ex.what()));
        return false;
    }

    const QString jobId = itemId.isEmpty() ? QStringLiteral("torrent:") + name : itemId;
    m_jobIds.insert(name, jobId);

    DownloadJob job;
    job.kind = DownloadJob::CollectionTorrent;
    job.itemId = jobId;
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
    try {
        m_session->post_torrent_updates();
        std::vector<lt::alert *> alerts;
        m_session->pop_alerts(&alerts);
        for (lt::alert *alert : alerts) {
            if (auto *update = lt::alert_cast<lt::state_update_alert>(alert)) {
                for (const auto &st : update->status) {
                    DownloadJob job;
                    job.kind = DownloadJob::CollectionTorrent;
                    const QString name = QString::fromStdString(st.name);
                    job.itemId = jobIdForName(name);
                    job.displayName = name;
                    job.bytesDone = st.total_wanted_done;
                    job.bytesTotal = st.total_wanted;
                    if (st.is_finished)
                        job.status = QStringLiteral("done");
                    else if (st.errc)
                        job.status = QStringLiteral("error");
                    else if (st.flags & lt::torrent_flags::paused)
                        job.status = QStringLiteral("paused");
                    else
                        job.status = QStringLiteral("running");
                    if (st.errc)
                        job.error = QString::fromStdString(st.errc.message());
                    emit jobUpdated(job);
                }
            } else if (auto *finished = lt::alert_cast<lt::torrent_finished_alert>(alert)) {
                const auto st = finished->handle.status();
                try {
                    finished->handle.pause();
                    finished->handle.save_resume_data(lt::torrent_handle::flush_disk_cache);
                } catch (const std::exception &) {
                }
                DownloadJob job;
                job.kind = DownloadJob::CollectionTorrent;
                const QString name = QString::fromStdString(st.name);
                job.itemId = jobIdForName(name);
                job.displayName = name;
                job.status = QStringLiteral("done");
                job.bytesDone = st.total_wanted_done;
                job.bytesTotal = st.total_wanted;
                emit jobUpdated(job);
                emit torrentCompleted(job.displayName, QString::fromStdString(st.save_path));
            } else if (auto *err = lt::alert_cast<lt::torrent_error_alert>(alert)) {
                QString name;
                try {
                    name = QString::fromStdString(err->handle.status().name);
                } catch (const std::exception &) {
                }
                if (name.isEmpty())
                    name = QString::fromStdString(err->message());
                emit torrentFailed(name, QString::fromStdString(err->error.message()));
            } else if (auto *rd = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
                const QString savePath = QString::fromStdString(rd->params.save_path);
                writeResumeFile(savePath, hashToHex(rd->params.info_hashes),
                                lt::write_resume_data_buf(rd->params));
                if (m_pendingResume > 0)
                    --m_pendingResume;
            } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
                if (m_pendingResume > 0)
                    --m_pendingResume;
            }
        }
    } catch (const std::exception &ex) {
        emit torrentFailed(QStringLiteral("session"), QString::fromUtf8(ex.what()));
    }
#endif
}
