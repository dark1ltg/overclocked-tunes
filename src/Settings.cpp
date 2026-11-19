#include "Settings.h"

#include "OfficialTorrentEngine.h"

#include <QSettings>
#include <algorithm>

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

QString Settings::libraryPath() const
{
    return QSettings().value(QStringLiteral("libraryPath")).toString();
}

void Settings::setLibraryPath(const QString &path)
{
    QSettings().setValue(QStringLiteral("libraryPath"), path);
}

int Settings::concurrency() const
{
    return std::clamp(QSettings().value(QStringLiteral("concurrency"), 1).toInt(), 1, 3);
}

void Settings::setConcurrency(int value)
{
    QSettings().setValue(QStringLiteral("concurrency"), std::clamp(value, 1, 3));
}

bool Settings::includeAlbums() const
{
    return QSettings().value(QStringLiteral("includeAlbums"), true).toBool();
}

void Settings::setIncludeAlbums(bool value)
{
    QSettings().setValue(QStringLiteral("includeAlbums"), value);
}

bool Settings::preferTorrents() const
{
    return QSettings().value(QStringLiteral("preferTorrents"), true).toBool();
}

void Settings::setPreferTorrents(bool value)
{
    QSettings().setValue(QStringLiteral("preferTorrents"), value);
}

bool Settings::openTorrents() const
{
    const bool defaultOpen = !OfficialTorrentEngine::available();
    return QSettings().value(QStringLiteral("openTorrents"), defaultOpen).toBool();
}

void Settings::setOpenTorrents(bool value)
{
    QSettings().setValue(QStringLiteral("openTorrents"), value);
}

bool Settings::downloadOfficialTorrentsInApp() const
{
    return QSettings().value(QStringLiteral("downloadOfficialTorrentsInApp"), true).toBool();
}

void Settings::setDownloadOfficialTorrentsInApp(bool value)
{
    QSettings().setValue(QStringLiteral("downloadOfficialTorrentsInApp"), value);
}

int Settings::requestDelayMs() const
{
    return std::clamp(QSettings().value(QStringLiteral("requestDelayMs"), 400).toInt(), 250, 2000);
}

void Settings::setRequestDelayMs(int value)
{
    QSettings().setValue(QStringLiteral("requestDelayMs"), std::clamp(value, 250, 2000));
}

bool Settings::menuInstallPrompted() const
{
    return QSettings().value(QStringLiteral("menuInstallPrompted"), false).toBool();
}

void Settings::setMenuInstallPrompted(bool value)
{
    QSettings().setValue(QStringLiteral("menuInstallPrompted"), value);
}

bool Settings::menuInstalled() const
{
    return QSettings().value(QStringLiteral("menuInstalled"), false).toBool();
}

void Settings::setMenuInstalled(bool value)
{
    QSettings().setValue(QStringLiteral("menuInstalled"), value);
}
