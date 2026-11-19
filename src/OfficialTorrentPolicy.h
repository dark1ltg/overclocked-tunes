#pragma once

#include <QString>

namespace OfficialTorrentPolicy {

// HTTPS/HTTP/UDP URLs on bt.ocremix.org (and ocremix.org trackers).
bool isOfficialTorrentUrl(const QString &url);
bool isOfficialTrackerUrl(const QString &url);

} // namespace OfficialTorrentPolicy
