# OCR Catalog

Cross-platform Qt 6 desktop app that catalogs and downloads the public [OverClocked ReMix](https://ocremix.org/) library: individual ReMixes and official OC ReMix albums.

This project lives on GitHub: [dark1ltg/overclocked-tunes](https://github.com/dark1ltg/overclocked-tunes).

OCR Catalog is not affiliated with OverClocked ReMix. It only uses download links the site already publishes. Please keep request rates polite. For a first-time copy of the older back-catalog, OC ReMix recommends their [official torrents](https://ocremix.org/torrents/).

## What it does

- Syncs the public ReMix list (`/remixes/`) and album list (`/albums/`) into a local SQLite catalog
- Search by title, game, artist, or OCR ID
- Download selected items, or everything still missing
- ReMix MP3s come from official mirrors (`ocr.blueblue.fr`, `iterations.org`, `ocrmirror.org`), with fallback
- Official albums (`OCRA` / `OCRI` / `OCRC`) download as ZIP when the album site lists one; otherwise the `.torrent` is saved
- Skips files you already have
- Optional local playback of downloaded MP3s
- On Linux AppImage: **Install to application menu** so it appears in KDE Plasma, GNOME, and other Freedesktop desktops

## Clone

```bash
git clone https://github.com/dark1ltg/overclocked-tunes.git
cd overclocked-tunes
```

## Build (all platforms)

Needs CMake 3.16+, a C++17 compiler, and Qt 6 (Widgets, Network, Sql with SQLite, Svg; Multimedia optional).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The binary is `build/OCRCatalog` (Linux), `build/OCRCatalog.exe` (Windows), or `build/OCRCatalog.app` (macOS).

Headless live smoke (network required):

```bash
QT_QPA_PLATFORM=offscreen ./build/OCRCatalog --smoke
```

### Linux packages

Ubuntu / Debian:

```bash
sudo apt install qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite \
  qt6-svg-dev qt6-multimedia-dev cmake ninja-build g++
```

Fedora:

```bash
sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel qt6-qtmultimedia-devel cmake ninja-build gcc-c++
```

### Windows

Install [Qt 6](https://www.qt.io/download) and Visual Studio Build Tools, then:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.7.3\msvc2019_64"
cmake --build build --config Release
.\packaging\windows\build.ps1
```

That produces `dist/OCRCatalog-windows.zip` (portable `OCRCatalog.exe`) and, if NSIS is installed, `dist/OCRCatalog-setup.exe`.

### macOS

```bash
brew install qt cmake
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./packaging/macos/build-dmg.sh
```

## Linux AppImage and the application menu

Build a menu-installable AppImage:

```bash
python3 packaging/linux/generate-icon.py
chmod +x packaging/linux/build-appimage.sh
./packaging/linux/build-appimage.sh
```

The AppImage contains a Freedesktop `.desktop` file, hicolor icons, and AppStream metadata.

1. Mark it executable and run it: `chmod +x OCRCatalog-*.AppImage && ./OCRCatalog-*.AppImage`
2. Choose **Desktop → Install to application menu** (or accept the first-run prompt)
3. It copies the AppImage to `~/Applications/OCRCatalog.AppImage` and writes `~/.local/share/applications/org.ocrcatalog.OCRCatalog.desktop`

KDE Plasma Kickoff, GNOME Overview, and similar menus pick that up. [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher) also works if you prefer it.

## GitHub Releases

Tag a version on `main` (for example `v1.0.0`) or run the **Release** workflow. GitHub Actions builds:

- Linux `.AppImage`
- Windows portable zip + installer `.exe`
- macOS `.dmg`

## Library layout

After you pick a library folder:

```
library/
  remixes/     official MP3 filenames
  albums/      ZIP files when HTTP mirrors exist
  torrents/    .torrent files when that is the published option
```

The local catalog database is stored in the Qt app-data directory, not in the library folder.
