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
CMAKE_ARGS=(-S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr)
if [[ -n "${CXX_COMPILER}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER="${CXX_COMPILER}")
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD}" -j"$(nproc)"
DESTDIR="${APPDIR}" cmake --install "${BUILD}"

ICON_SRC="${ROOT}/src/resources/icons/org.ocrcatalog.OCRCatalog.svg"
install -Dm644 "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/org.ocrcatalog.OCRCatalog.svg"
install -Dm644 "${ROOT}/packaging/linux/org.ocrcatalog.OCRCatalog.desktop" \
  "${APPDIR}/usr/share/applications/org.ocrcatalog.OCRCatalog.desktop"
install -Dm644 "${ROOT}/packaging/linux/org.ocrcatalog.OCRCatalog.metainfo.xml" \
  "${APPDIR}/usr/share/metainfo/org.ocrcatalog.OCRCatalog.metainfo.xml"

if [[ -f "${ROOT}/packaging/linux/icons/org.ocrcatalog.OCRCatalog.png" ]]; then
  install -Dm644 "${ROOT}/packaging/linux/icons/org.ocrcatalog.OCRCatalog.png" \
    "${APPDIR}/usr/share/icons/hicolor/256x256/apps/org.ocrcatalog.OCRCatalog.png"
  cp "${ROOT}/packaging/linux/icons/org.ocrcatalog.OCRCatalog.png" "${APPDIR}/org.ocrcatalog.OCRCatalog.png"
fi
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

export QMAKE="$(command -v qmake6 || command -v qmake)"
export LINUXDEPLOY_OUTPUT_VERSION="${LINUXDEPLOY_OUTPUT_VERSION:-1.0.0}"
cd "${BUILD}"
# Extract linuxdeploy if FUSE is unavailable (common in CI/containers).
run_ld() {
  if "${LINUXDEPLOY}" --appimage-extract-and-run "$@" 2>/dev/null; then
    return 0
  fi
  "${LINUXDEPLOY}" "$@"
}
run_ld \
  --appdir "${APPDIR}" \
  --desktop-file "${APPDIR}/org.ocrcatalog.OCRCatalog.desktop" \
  --icon-file "${APPDIR}/org.ocrcatalog.OCRCatalog.png" \
  --plugin qt \
  --output appimage

mkdir -p "${DIST}"
shopt -s nullglob
for image in "${BUILD}"/*.AppImage OCRCatalog*.AppImage; do
  if [[ -f "${image}" && "${image}" != *linuxdeploy* ]]; then
    cp "${image}" "${DIST}/OCRCatalog-${LINUXDEPLOY_OUTPUT_VERSION}-${ARCH}.AppImage"
  fi
done
ls -l "${DIST}"
