# OCR Catalog

Cross-platform Qt 6 desktop app that catalogs and downloads the public [OverClocked ReMix](https://ocremix.org/) library: individual ReMixes and official OC ReMix albums.

This project lives on GitHub: [dark1ltg/overclocked-tunes](https://github.com/dark1ltg/overclocked-tunes).

OCR Catalog is not affiliated with OverClocked ReMix. It only uses download links the site already publishes. Please keep request rates polite. For a first-time copy of the older back-catalog, OC ReMix recommends their [official torrents](https://ocremix.org/torrents/).

## What it does

- Syncs the public ReMix list (`/remixes/`) and album list (`/albums/`) into a local SQLite catalog
- Search by title, game, artist, or OCR ID
- **Download missing** starts with the two official collection torrents ([OCR 1–4000](https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_1_to_4000_[v20201028].torrent) and [4001–4473](https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_4001_to_4473_(Q2_2023_update).torrent)), then HTTPS mirrors only for remixes after OCR 4473 and albums those packs do not list
- Linux builds download those official torrents **inside the app** (libtorrent). Only `https://bt.ocremix.org/torrents/*.torrent` is accepted; there is no magnet box, file picker, or way to add other torrents
- Selected items still fetch from the site's HTTPS mirrors when you ask for a specific track
- Official albums (`OCRA` / `OCRI` / `OCRC`) prefer the tracker `.torrent`; ZIP mirrors are used only when no torrent exists
- Skips files you already have
- Optional local playback of downloaded MP3s
- On Linux AppImage: **Install to application menu** so it appears in KDE Plasma, GNOME, and other Freedesktop desktops

## Clone

```bash
git clone https://github.com/dark1ltg/overclocked-tunes.git
cd overclocked-tunes
```

## Build (all platforms)

Needs CMake 3.16+, a C++17 compiler, and Qt 6 (Widgets, Network, Sql with SQLite, Svg; Multimedia optional). Linux in-app torrent downloads also need `libtorrent-rasterbar` (detected via pkg-config). Windows and macOS builds still save the official `.torrent` files without embedding a torrent engine.

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
  qt6-svg-dev qt6-multimedia-dev cmake ninja-build g++ \
  libtorrent-rasterbar-dev pkg-config
```

Fedora:

```bash
sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel qt6-qtmultimedia-devel \
  cmake ninja-build gcc-c++ libtorrent-rasterbar-devel
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

The [Releases](https://github.com/dark1ltg/overclocked-tunes/releases) page is where the desktop builds go. Each version attaches three files:

- Linux `.AppImage`
- Windows installer `.exe`
- macOS `.dmg`

GitHub also adds source zip/tar.gz on that page; those are not the app. To publish a new version, set `.github/release-version` to a tag like `v1.0.1` on `main`, push a `v*` tag, or run **Cut GitHub release**.

## Library layout

After you pick a library folder:

```
library/
  remixes/       official MP3 filenames (HTTPS mirrors, OCR after the collection cutoff)
  albums/        ZIP files when you choose a specific album and a ZIP mirror exists
  torrents/      official collection .torrents (OCR 1–4473) plus album .torrents
  collections/   files the in-app engine writes from those official torrents (Linux)
```

OCR Catalog is not a general BitTorrent client. On Linux it embeds libtorrent only to fetch the official OC ReMix collection and album torrents from `bt.ocremix.org`. Anything else is rejected. Windows and macOS builds (and Linux builds without libtorrent) still save those `.torrent` files so you can open them in qBittorrent, Transmission, or Deluge. Album torrents are not auto-opened in an external client.

The local catalog database is stored in the Qt app-data directory, not in the library folder.
