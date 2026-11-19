#include "DesktopIntegrator.h"

#include "appinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QSvgRenderer>

bool DesktopIntegrator::isRunningAsAppImage()
{
    return !qEnvironmentVariableIsEmpty("APPIMAGE");
}

QString DesktopIntegrator::appImagePath()
{
    return qEnvironmentVariable("APPIMAGE");
}

QString DesktopIntegrator::installedAppImagePath()
{
    return QDir::homePath() + QStringLiteral("/Applications/OCRCatalog.AppImage");
}

QString DesktopIntegrator::desktopFilePath()
{
    return QDir::homePath() + QStringLiteral("/.local/share/applications/")
        + QString::fromLatin1(AppInfo::id()) + QStringLiteral(".desktop");
}

bool DesktopIntegrator::isInstalled()
{
    return QFileInfo::exists(desktopFilePath()) && QFileInfo::exists(installedAppImagePath());
}

static bool writeIconPng(const QString &path, int size)
{
    QSvgRenderer renderer(QStringLiteral(":/icons/org.ocrcatalog.OCRCatalog.svg"));
    if (!renderer.isValid())
        return false;
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();
    QDir().mkpath(QFileInfo(path).absolutePath());
    return image.save(path, "PNG");
}

bool DesktopIntegrator::install(QString *error)
{
    const QString source = isRunningAsAppImage() ? appImagePath() : QCoreApplication::applicationFilePath();
    if (source.isEmpty() || !QFileInfo::exists(source)) {
        if (error)
            *error = QStringLiteral("Could not locate the running application.");
        return false;
    }

    const QString dest = installedAppImagePath();
    QDir().mkpath(QFileInfo(dest).absolutePath());
    QFile::remove(dest);
    if (!QFile::copy(source, dest)) {
        if (error)
            *error = QStringLiteral("Could not copy the app to %1").arg(dest);
        return false;
    }
    QFile(dest).setPermissions(QFile(dest).permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup
                               | QFileDevice::ExeOther);

    const QString iconDir = QDir::homePath() + QStringLiteral("/.local/share/icons/hicolor");
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    for (int size : sizes) {
        const QString png = QStringLiteral("%1/%2x%2/apps/%3.png")
                                .arg(iconDir)
                                .arg(size)
                                .arg(QString::fromLatin1(AppInfo::id()));
        writeIconPng(png, size);
    }
    QDir().mkpath(iconDir + QStringLiteral("/scalable/apps"));
    QFile::copy(QStringLiteral(":/icons/org.ocrcatalog.OCRCatalog.svg"),
                iconDir + QStringLiteral("/scalable/apps/") + QString::fromLatin1(AppInfo::id())
                    + QStringLiteral(".svg"));

    QString desktop = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Version=1.0\n"
        "Name=OCR Catalog\n"
        "Comment=Catalog and download OC ReMix songs and albums\n"
        "Exec=\"@EXEC@\"\n"
        "Icon=@ICON@\n"
        "Terminal=false\n"
        "StartupNotify=true\n"
        "StartupWMClass=OCRCatalog\n"
        "Categories=AudioVideo;Audio;Qt;\n"
        "Keywords=OCRemix;OC ReMix;VGM;remix;\n");
    if (!dest.contains(QLatin1Char(' ')))
        desktop += QStringLiteral("TryExec=@EXEC@\n");
    desktop.replace(QStringLiteral("@EXEC@"), dest);
    desktop.replace(QStringLiteral("@ICON@"), QString::fromLatin1(AppInfo::id()));

    QDir().mkpath(QFileInfo(desktopFilePath()).absolutePath());
    QFile file(desktopFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Could not write %1").arg(desktopFilePath());
        return false;
    }
    file.write(desktop.toUtf8());
    file.close();
    refreshMenus();
    return true;
}

bool DesktopIntegrator::uninstall(QString *error)
{
    Q_UNUSED(error);
    QFile::remove(desktopFilePath());
    QFile::remove(installedAppImagePath());
    refreshMenus();
    return true;
}

void DesktopIntegrator::refreshMenus()
{
    const QString apps = QDir::homePath() + QStringLiteral("/.local/share/applications");
    QProcess::startDetached(QStringLiteral("update-desktop-database"), {apps});
    QProcess::startDetached(QStringLiteral("xdg-desktop-menu"), {QStringLiteral("forceupdate")});
    QProcess::startDetached(QStringLiteral("gtk-update-icon-cache"),
                            {QDir::homePath() + QStringLiteral("/.local/share/icons/hicolor")});
}
