#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build/macos"
DIST="${ROOT}/dist"
mkdir -p "${BUILD}" "${DIST}"
CMAKE_ARGS=(-S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release)
if [[ -n "${QT_ROOT_DIR:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${QT_ROOT_DIR}")
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD}" -j"$(sysctl -n hw.ncpu)"
APP="${BUILD}/OCRCatalog.app"
macdeployqt "${APP}"
# Copy plugins into the .app BEFORE wrapping it in a DMG.
QT_PLUGINS="$("$(command -v qmake6 || command -v qmake)" -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
if [[ -n "${QT_PLUGINS}" ]]; then
  if [[ -d "${QT_PLUGINS}/sqldrivers" ]]; then
    mkdir -p "${APP}/Contents/PlugIns/sqldrivers"
    cp -R "${QT_PLUGINS}/sqldrivers/." "${APP}/Contents/PlugIns/sqldrivers/"
  fi
  if [[ -d "${QT_PLUGINS}/tls" ]]; then
    mkdir -p "${APP}/Contents/PlugIns/tls"
    cp -R "${QT_PLUGINS}/tls/." "${APP}/Contents/PlugIns/tls/"
  fi
fi
macdeployqt "${APP}" -dmg
cp "${BUILD}/OCRCatalog.dmg" "${DIST}/OCRCatalog-macos.dmg"
ls -l "${DIST}"
