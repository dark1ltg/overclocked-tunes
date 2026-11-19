#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build/macos"
DIST="${ROOT}/dist"
mkdir -p "${BUILD}" "${DIST}"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD}" -j"$(sysctl -n hw.ncpu)"
APP="${BUILD}/OCRCatalog.app"
macdeployqt "${APP}" -dmg
cp "${BUILD}/OCRCatalog.dmg" "${DIST}/OCRCatalog-macos.dmg"
ls -l "${DIST}"
