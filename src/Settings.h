#pragma once

#include <QString>

class Settings {
public:
    static Settings &instance();

    QString libraryPath() const;
    void setLibraryPath(const QString &path);

    int concurrency() const;
    void setConcurrency(int value);

    bool includeAlbums() const;
    void setIncludeAlbums(bool value);

    bool preferTorrents() const;
    void setPreferTorrents(bool value);

    bool openTorrents() const;
    void setOpenTorrents(bool value);

    bool downloadOfficialTorrentsInApp() const;
    void setDownloadOfficialTorrentsInApp(bool value);

    int requestDelayMs() const;
    void setRequestDelayMs(int value);

    bool menuInstallPrompted() const;
    void setMenuInstallPrompted(bool value);

    bool menuInstalled() const;
    void setMenuInstalled(bool value);

private:
    Settings() = default;
};
