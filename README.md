# OCR Catalog

A desktop app for the public [OC ReMix](https://ocremix.org/) library. Search remixes and official albums, keep a local catalog, and download what you do not already have.

OCR Catalog is not affiliated with OverClocked ReMix. It only uses download links the site already publishes, and it stays polite about request rates.

![ReMixes tab in OCR Catalog](docs/screenshots/remixes.png)

## Download

Use a ready-made build from [Releases](https://github.com/dark1ltg/overclocked-tunes/releases/latest).

| You use | Grab this |
| --- | --- |
| Linux | `OCRCatalog-v1.0.1-x86_64.AppImage` from [v1.0.1](https://github.com/dark1ltg/overclocked-tunes/releases/tag/v1.0.1) |
| Windows | `OCRCatalog-v1.0.0-setup.exe` from [v1.0.0](https://github.com/dark1ltg/overclocked-tunes/releases/tag/v1.0.0) |
| macOS | `OCRCatalog-v1.0.1-macos.dmg` from [v1.0.1](https://github.com/dark1ltg/overclocked-tunes/releases/tag/v1.0.1) |

Each one is a self-contained app. Qt and the libraries it needs are inside the file. GitHub also attaches source zip/tar.gz on that page; those are the code, not the app.

On Linux, mark the AppImage executable and run it. **Desktop → Install to application menu** puts it in KDE Plasma, GNOME, and other Freedesktop menus.

## Using it

First launch looks like this until you sync:

![Empty catalog, before the first sync](docs/screenshots/empty.png)

1. **Library…** — pick a folder for music, albums, and official torrents.
2. **Sync catalog** — pulls the public remix and album lists into a local SQLite catalog.
3. Search by title, game, artist, or OCR ID.
4. **Download missing** — official collection torrents first, then HTTPS for anything those packs do not cover.
5. Select a row for details. **Play** plays a downloaded MP3 (or opens the file if this build has no Qt Multimedia).

![Albums tab](docs/screenshots/albums.png)

**Download missing** starts with the two official packs OC ReMix publishes for the back-catalog:

- [OCR 1–4000](https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_1_to_4000_[v20201028].torrent)
- [OCR 4001–4473](https://bt.ocremix.org/torrents/OC_ReMix_Collection_-_4001_to_4473_(Q2_2023_update).torrent)

Linux and macOS v1.0.1 builds fetch those torrents **inside the app** (libtorrent). Only files from `https://bt.ocremix.org/torrents/` are accepted. There is no magnet box and no way to add other torrents. The current Windows installer is still [v1.0.0](https://github.com/dark1ltg/overclocked-tunes/releases/tag/v1.0.0); it saves the official `.torrent` files so you can open them in qBittorrent, Transmission, or Deluge. Remixes after OCR 4473, and albums those packs do not list, come from the site's HTTPS mirrors. Official albums prefer the tracker `.torrent`; a ZIP is used only when there is no torrent.

![Download queue](docs/screenshots/queue.png)

This is not a general BitTorrent client. If you turn off in-app torrents in Settings, the official `.torrent` files are still saved so you can open them in qBittorrent, Transmission, or Deluge.

![Settings](docs/screenshots/settings.png)

## Library folder

```
library/
  remixes/       MP3s fetched from HTTPS mirrors
  albums/        album ZIPs when a ZIP mirror exists
  torrents/      official collection and album .torrent files
  collections/   files the in-app engine writes from those torrents
```

The catalog database lives in the Qt app-data directory, not in this folder. Files you already have are skipped.

## Building from source

Skip this unless you are changing the code. Everyone else should use the release files above.

You need CMake 3.16+, a C++17 compiler, Qt 6 (Widgets, Network, Sql/SQLite, Svg), and libtorrent-rasterbar for in-app official torrents. Optional: Qt Multimedia for in-app playback. On Windows the release build uses vcpkg (`vcpkg.json`). On macOS, Homebrew `libtorrent-rasterbar` is enough.

```bash
git clone https://github.com/dark1ltg/overclocked-tunes.git
cd overclocked-tunes
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is `build/OCRCatalog` (Linux), `build/OCRCatalog.exe` (Windows), or `build/OCRCatalog.app` (macOS). Release packaging scripts live under `packaging/` if you are cutting a new GitHub release.
