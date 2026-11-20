#pragma once

#include <QString>

namespace AppInfo {
inline const char *id() { return "org.ocrcatalog.OCRCatalog"; }
inline const char *name() { return "OCR Catalog"; }
inline const char *organization() { return "OCRCatalog"; }
inline const char *version() { return "1.0.1"; }
inline const char *userAgent()
{
    return "OCRCatalog/1.0.1 (+https://github.com/dark1ltg/overclocked-tunes)";
}
inline const char *siteRoot() { return "https://ocremix.org"; }
inline QString iconName() { return QStringLiteral("org.ocrcatalog.OCRCatalog"); }
}
