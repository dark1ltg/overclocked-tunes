#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build/macos"
DIST="${ROOT}/dist"
mkdir -p "${BUILD}" "${DIST}"

# Xcode 16+ dropped AGL; Qt 6.8 still links it. A stub lets the .app link.
SDK_AGL="$(xcrun --show-sdk-path)/System/Library/Frameworks/AGL.framework"
STUB_ROOT="${BUILD}/Frameworks"
if [[ ! -d "${SDK_AGL}" ]]; then
  mkdir -p "${STUB_ROOT}/AGL.framework/Versions/A"
  echo 'void aglStub(void) {}' | clang -dynamiclib -x c - \
    -o "${STUB_ROOT}/AGL.framework/Versions/A/AGL" \
    -install_name /System/Library/Frameworks/AGL.framework/Versions/A/AGL \
    -compatibility_version 1 -current_version 1
  ln -sfn A "${STUB_ROOT}/AGL.framework/Versions/Current"
  ln -sfn Versions/Current/AGL "${STUB_ROOT}/AGL.framework/AGL"
  export CMAKE_FRAMEWORK_PATH="${STUB_ROOT}${CMAKE_FRAMEWORK_PATH:+:${CMAKE_FRAMEWORK_PATH}}"
fi

if command -v brew >/dev/null 2>&1; then
  brew install libtorrent-rasterbar pkg-config dylibbundler
  BREW_PREFIX="$(brew --prefix)"
  export PKG_CONFIG_PATH="${BREW_PREFIX}/lib/pkgconfig:${BREW_PREFIX}/opt/openssl@3/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  export CMAKE_PREFIX_PATH="${QT_ROOT_DIR:+${QT_ROOT_DIR}:}${BREW_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
fi

CMAKE_ARGS=(-S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DOCR_REQUIRE_TORRENT=ON)
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}")
elif [[ -n "${QT_ROOT_DIR:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${QT_ROOT_DIR}")
fi
if [[ -d "${STUB_ROOT}/AGL.framework" ]]; then
  CMAKE_ARGS+=(-DCMAKE_FRAMEWORK_PATH="${STUB_ROOT}")
  CMAKE_ARGS+=(-DCMAKE_EXE_LINKER_FLAGS="-F${STUB_ROOT}")
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD}" --target OCRCatalog -j"$(sysctl -n hw.ncpu)"
APP="${BUILD}/OCRCatalog.app"
macdeployqt "${APP}"
mkdir -p "${APP}/Contents/Frameworks"
if command -v dylibbundler >/dev/null 2>&1; then
  DYLIB_ARGS=(-od -b
    -x "${APP}/Contents/MacOS/OCRCatalog"
    -d "${APP}/Contents/Frameworks"
    -p @executable_path/../Frameworks)
  if command -v brew >/dev/null 2>&1; then
    BREW_PREFIX="$(brew --prefix)"
    DYLIB_ARGS+=(-s "${BREW_PREFIX}/lib" -s "${BREW_PREFIX}/opt/openssl@3/lib")
  fi
  dylibbundler "${DYLIB_ARGS[@]}"
else
  echo "dylibbundler is required to bundle libtorrent into the .app" >&2
  exit 1
fi
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
