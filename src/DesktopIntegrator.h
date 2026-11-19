#pragma once

#include <QString>

class DesktopIntegrator {
public:
    static bool isRunningAsAppImage();
    static QString appImagePath();
    static QString installedAppImagePath();
    static QString desktopFilePath();
    static bool isInstalled();
    static bool install(QString *error);
    static bool uninstall(QString *error);
    static void refreshMenus();
};
