#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build/appimage"
DIST="${ROOT}/dist"
ARCH="$(uname -m)"
APPDIR="${BUILD}/AppDir"

mkdir -p "${BUILD}" "${DIST}" "${APPDIR}"
# Prefer g++ when /usr/bin/c++ is clang without libstdc++ (common in this environment).
CXX_COMPILER="${CXX:-}"
if [[ -z "${CXX_COMPILER}" && -x /usr/bin/g++ ]]; then
  CXX_COMPILER=/usr/bin/g++
fi
CMAKE_ARGS=(-S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DOCR_REQUIRE_TORRENT=ON)
if [[ -n "${CXX_COMPILER}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER="${CXX_COMPILER}")
fi
if [[ -n "${QT_ROOT_DIR:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${QT_ROOT_DIR}")
  CMAKE_ARGS+=(-DCMAKE_BUILD_RPATH="${QT_ROOT_DIR}/lib")
  export LD_LIBRARY_PATH="${QT_ROOT_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD}" --target OCRCatalog -j"$(nproc)"
DESTDIR="${APPDIR}" cmake --install "${BUILD}"

ICON_SRC="${ROOT}/src/resources/icons/org.ocrcatalog.OCRCatalog.svg"
install -Dm644 "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/org.ocrcatalog.OCRCatalog.svg"
install -Dm644 "${ROOT}/packaging/linux/org.ocrcatalog.OCRCatalog.desktop" \
  "${APPDIR}/usr/share/applications/org.ocrcatalog.OCRCatalog.desktop"
install -Dm644 "${ROOT}/packaging/linux/org.ocrcatalog.OCRCatalog.metainfo.xml" \
  "${APPDIR}/usr/share/metainfo/org.ocrcatalog.OCRCatalog.metainfo.xml"

PNG="${ROOT}/packaging/linux/icons/org.ocrcatalog.OCRCatalog.png"
if [[ ! -f "${PNG}" ]]; then
  python3 "${ROOT}/packaging/linux/generate-icon.py"
fi
install -Dm644 "${PNG}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/org.ocrcatalog.OCRCatalog.png"
cp "${PNG}" "${APPDIR}/org.ocrcatalog.OCRCatalog.png"
cp "${ROOT}/packaging/linux/org.ocrcatalog.OCRCatalog.desktop" "${APPDIR}/org.ocrcatalog.OCRCatalog.desktop"

LINUXDEPLOY="${BUILD}/linuxdeploy-${ARCH}.AppImage"
PLUGIN="${BUILD}/linuxdeploy-plugin-qt-${ARCH}.AppImage"
if [[ ! -x "${LINUXDEPLOY}" ]]; then
  curl -fsSL -o "${LINUXDEPLOY}" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
  curl -fsSL -o "${PLUGIN}" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage"
  chmod +x "${LINUXDEPLOY}" "${PLUGIN}"
fi

if [[ -n "${QT_ROOT_DIR:-}" && -x "${QT_ROOT_DIR}/bin/qmake" ]]; then
  export QMAKE="${QT_ROOT_DIR}/bin/qmake"
else
  export QMAKE="$(command -v qmake6 || command -v qmake)"
fi
if [[ -z "${QMAKE}" ]]; then
  echo "qmake not found; Qt must be on PATH or QT_ROOT_DIR" >&2
  exit 1
fi
export LINUXDEPLOY_OUTPUT_VERSION="${LINUXDEPLOY_OUTPUT_VERSION:-1.0.0}"
export EXTRA_QT_PLUGINS="${EXTRA_QT_PLUGINS:-tls;iconengines;imageformats}"
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="${BUILD}:${PATH}"

# linuxdeploy-plugin-qt deploys every sqldrivers plugin. MySQL/ODBC/Mimer then
# fail the build (missing libmimerapi.so) and would bloat the AppImage.
SQL_PLUGINS="$("${QMAKE}" -query QT_INSTALL_PLUGINS)/sqldrivers"
if [[ -d "${SQL_PLUGINS}" ]]; then
  chmod -R u+w "${SQL_PLUGINS}" || true
  find "${SQL_PLUGINS}" -type f ! -name '*sqlite*' -print -delete
fi

LD_ARGS=()
if [[ -n "${QT_ROOT_DIR:-}" && -d "${QT_ROOT_DIR}/lib" ]]; then
  shopt -s nullglob
  for lib in "${QT_ROOT_DIR}/lib"/libicu*.so*; do
    if [[ -f "${lib}" && ! -L "${lib}" ]]; then
      LD_ARGS+=(--library "${lib}")
    fi
  done
  shopt -u nullglob
fi

cd "${BUILD}"
"${LINUXDEPLOY}" \
  --appdir "${APPDIR}" \
  --desktop-file "${APPDIR}/org.ocrcatalog.OCRCatalog.desktop" \
  --icon-file "${APPDIR}/org.ocrcatalog.OCRCatalog.png" \
  --plugin qt \
  "${LD_ARGS[@]}" \
  --output appimage

mkdir -p "${DIST}"
shopt -s nullglob
copied=0
for image in "${BUILD}"/*.AppImage OCRCatalog*.AppImage; do
  if [[ -f "${image}" && "${image}" != *linuxdeploy* ]]; then
    cp "${image}" "${DIST}/OCRCatalog-${LINUXDEPLOY_OUTPUT_VERSION}-${ARCH}.AppImage"
    copied=1
  fi
done
if [[ "${copied}" -eq 0 ]]; then
  echo "linuxdeploy did not produce an AppImage" >&2
  ls -la "${BUILD}" >&2 || true
  exit 1
fi
ls -l "${DIST}"
