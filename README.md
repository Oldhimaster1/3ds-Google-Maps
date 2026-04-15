# 3DS Google Maps

An interactive map viewer for Nintendo 3DS homebrew. Browse OpenStreetMap and Esri satellite imagery directly on your 3DS, track your location with GPS via your phone, search for places, plan routes, and save favorites — all running natively on the handheld.

The app downloads map tiles over HTTPS using libcurl and mbedTLS (no proxy or external server needed). Satellite JPEG tiles are decoded on background threads so the map stays smooth, even on the original 3DS's 268 MHz CPU.

> **New in v2.0**: Offline tile caching — download an entire region's worth of map tiles on your PC and transfer them to the SD card as a single file. No more needing WiFi every time you want to look at the map.

---

## Features

### Map Display
- **Dual screen** — top screen shows the map, bottom screen has the controls and status bar
- **Touch panning** — drag on the bottom screen to scroll around
- **D-pad and Circle Pad** — pan in any direction; the Circle Pad gives smooth analog movement
- **L/R shoulder buttons** — zoom in and out (zoom levels 1–18)
- **Crosshair** — a small red cross always marks the exact center of the map
- **Night mode** — a dark overlay that's easier on the eyes at night
- **Debug overlay** — optional display showing zoom level, tile cache stats, and frame rate

### Tile Sources
- **OpenStreetMap** — street map tiles via `tile.openstreetmap.org`, decoded with libpng
- **Esri World Imagery** — satellite photo tiles, decoded with stb_image on worker threads to keep the UI responsive
- **Esri World Street Map** — an alternative street map style, also switchable in settings
- **SD card cache** — every tile you view gets saved to the SD card, so it loads instantly next time — even without WiFi
- **RAM cache** — the 64 most recently used tiles stay in memory for instant display
- **Prefetch ring** — optionally pre-downloads tiles around your current view so panning feels seamless

### Offline Tile Caching
- **Tilepack files** — download thousands of tiles on your PC and pack them into a single `.tilepack` file, then copy just that one file to your SD card (way faster than transferring 15,000 individual PNGs over FAT32)
- **PC download tool** — `tools/download_region.py` handles the downloading, supports Esri satellite and street tiles, runs 8 parallel workers, and automatically deduplicates identical tiles (like ocean and empty land) to save space
- **In-app download** — there's also a "Download Region" button in the Data settings tab that downloads tiles for your current map view directly on the 3DS (handy for smaller areas, but the PC tool is faster for big regions)
- **Automatic loading** — the app checks for `sat.tilepack` and `street.tilepack` on startup and uses them as a tile source alongside the regular cache and live downloads

### GPS via Phone
- **HTTPS server mode** — the 3DS spins up a TLS 1.2 server on port 8443; open the URL on your phone's browser and it streams your GPS coordinates back to the 3DS using the Geolocation API
- **QR code** — when the server starts, a scannable QR code pops up on the top screen so you don't have to type the URL
- **NMEA TCP client** — alternatively, connect to a GPS2IP or similar NMEA forwarding app on your phone
- **Auto-follow** — toggle tracking mode and the map automatically centers on your current position

### Search & Navigation
- **Place search** — look up addresses and points of interest (powered by Nominatim/OpenStreetMap geocoding)
- **Route planning** — enter a start and destination to get a route drawn on the map (via OSRM)
- **Reverse geocode** — tap "Identify" to find out the name/address of whatever's at the center of the map
- **Favorites** — save locations you visit often, stored to `sdmc:/3ds_google_maps/favorites.txt`
- **Panorama viewer** — basic street-level panorama support (requires the debug proxy)

### Settings
Full settings menu on the bottom screen, organized into four tabs:
- **View** — night mode, debug overlay, crosshair, zoom reset
- **Tiles** — switch between OSM / Esri satellite / Esri street, clear disk cache, adjust prefetch ring size
- **Data** — route planning, place search, reverse geocode, favorites, offline download
- **GPS** — choose NMEA client or HTTPS server mode, start/stop GPS, see the connection URL and QR code

---

## Controls

| Input | Action |
|-------|--------|
| D-Pad | Pan the map |
| Circle Pad | Smooth analog pan |
| L / R | Zoom in / out |
| Touch drag | Pan the map |
| Bottom panel buttons | Search, Favorites, Recents, Route, GPS, Track, Menu |
| A | Toggle GPS tracking |
| B | Close settings / go back |
| START | Exit the app |

---

## Offline Map Caching

If you want to use the map without WiFi (or just want tiles to load instantly), you can pre-download an entire region on your PC.

### Quick start

Make sure you have Python 3 installed, then run:

```bash
# Download satellite tiles for a 10 km radius around New York City, zoom levels 10–14
python tools/download_region.py --lat 40.7128 --lon -74.006 --radius-km 10 --zoom 10-14

# Download street map tiles for South Carolina using a bounding box
python tools/download_region.py --bbox 32.03,-83.36,35.22,-78.54 --zoom 6-13 --street

# Bigger region with more parallel workers, skip confirmation prompt
python tools/download_region.py --lat 35.5 --lon -80.0 --radius-km 200 --zoom 6-12 -w 16 -y
```

The script creates a `.tilepack` file (by default under `./3ds_google_maps/tiles/` in the current directory — matching the SD card folder layout). Copy it to your 3DS SD card:

```
SD card:
  sdmc:/3ds_google_maps/tiles/sat.tilepack      ← satellite tiles
  sdmc:/3ds_google_maps/tiles/street.tilepack    ← street map tiles
```

The app picks these up automatically on startup. You can re-run the script for additional areas and it will merge new tiles into the existing pack.

### Why a single file instead of individual tiles?

Transferring 15,000 small PNG files to a FAT32-formatted SD card can take over an hour because of per-file overhead. A single `.tilepack` archive transfers in under a minute and the app reads from it using a binary search index — lookups are O(log n).

### Tile deduplication

The download tool automatically detects duplicate tiles (ocean tiles, empty land, etc.) using SHA-256 hashing and stores each unique tile only once. For large coastal or ocean-heavy regions this can cut the file size by 20–50%.

### What about OpenStreetMap tiles?

The download tool uses **Esri** tile servers (World Imagery for satellite, World Street Map for streets) because OpenStreetMap's tile usage policy prohibits bulk downloading. Live browsing on the 3DS still uses OSM tiles for street view, which is fine for normal interactive use.

---

## GPS Phone Bridge

The 3DS doesn't have a built-in GPS, but your phone does. Here's how the bridge works:

1. Open Settings → GPS tab on the 3DS and tap **Server**
2. The 3DS starts an HTTPS server on port 8443 and shows a QR code on the top screen
3. Scan the QR code with your phone — it opens a page in your browser
4. The browser asks for location permission; allow it
5. Your phone sends GPS coordinates to the 3DS every second via `POST /location`
6. The map follows your real position in real time

The TLS certificate is self-signed (baked into the app binary), so your browser will show a security warning the first time — just tap "Advanced → Proceed anyway" and you're good.

Both your phone and 3DS need to be on the same WiFi network.

---

## Building from Source

### Prerequisites

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS toolchain, then grab the required libraries:

```bash
dkp-pacman -S 3ds-dev
dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-libpng
```

### Build

In the devkitPro MSYS2 shell:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

cd /path/to/3ds_google_maps
make
```

On Windows (devkitPro installed to `C:\devkitPro`):

```bash
export DEVKITPRO=/c/devkitPro
export DEVKITARM=/c/devkitPro/devkitARM
export PATH=/c/devkitPro/devkitARM/bin:/c/devkitPro/tools/bin:$PATH
cd /c/path/to/3ds_google_maps
make
```

Output: `3ds_google_maps.3dsx`

### Libraries linked

```
-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpng -lz -lcitro2d -lcitro3d -lctru -lm
```

---

## Installation

1. Copy `3ds_google_maps.3dsx` to `sdmc:/3ds/3ds_google_maps/` on your 3DS SD card
2. (Optional) Copy any `.tilepack` files you generated to `sdmc:/3ds_google_maps/tiles/`
3. Launch via the Homebrew Launcher
4. WiFi is needed for live tile downloads and GPS — but if you have tilepacks, the map works offline

### Data paths on SD card

```
sdmc:/3ds_google_maps/
  tiles/                 — cached map tiles (auto-created)
  tiles/sat.tilepack     — offline satellite tile pack (optional)
  tiles/street.tilepack  — offline street tile pack (optional)
  favorites.txt          — saved locations
  log.txt                — debug log (overwritten each launch)
```

---

## File Structure

```
source/
  main.c          — Main loop, UI rendering, settings panel, input handling, offline download
  network.c       — Tile downloading (libcurl HTTPS + httpc HTTP fallback), disk cache, tilepack integration
  map_tiles.c     — Tile coordinate math, RAM cache, decode pipeline, prefetch, citro2d rendering
  gps.c           — GPS source mode dispatch (NMEA client vs HTTPS server)
  gps_server.c    — HTTPS GPS server (mbedTLS TLS 1.2, serves HTML page, receives location POSTs)
  tilepack.c      — Offline tilepack reader (binary search index, thread-safe file reads)
  qrcode.c        — Self-contained QR code encoder (byte mode, EC level L, versions 1–4)
  simple_png.c    — PNG loader (libpng → Morton-tiled ABGR for GPU upload)
  logging.c       — File logger → sdmc:/3ds_google_maps/log.txt
  stb_image.c     — stb_image build unit (JPEG decode for satellite tiles)

include/
  tilepack.h        — Tilepack file format spec and API
  network.h         — Tile download API, TileSource enum
  map_tiles.h       — Tile rendering and caching API
  gps.h             — GPS mode API (init/cleanup/fix access)
  gps_server.h      — GPS HTTPS server API
  gps_server_cert.h — Embedded self-signed DER cert + RSA-2048 key (valid 2026–2036)
  qrcode.h          — qr_encode() API
  logging.h         — log_write() API
  simple_png.h      — PNG loader header
  stb_image_all.h   — stb_image v2.30 (full JPEG/PNG decoder)
  stb_image.h       — stb_image public API header

tools/
  download_region.py — Bulk tile downloader for offline tilepacks (run on PC)
  tile_proxy.py      — Local debug proxy for tile requests (dev use only)
  generate_cert.py   — Generated the embedded TLS cert/key (don't need to run again)

Makefile             — devkitARM cross-compilation config
```

---

## Technical Notes

A few things worth knowing if you're working on the code or just curious about how it ticks:

- **TLS on 3DS**: The built-in `httpc` service only speaks TLS 1.0, which modern tile servers reject. libcurl with mbedTLS handles TLS 1.2+ properly. OpenSSL was tried early on but its PRNG can't seed on 3DS hardware — mbedTLS uses `sslcGenerateRandomData()` from the 3DS SSL system service instead.

- **Tile decode threading**: Satellite JPEG tiles take 1–2 seconds to decode on the Old 3DS CPU. To keep the UI smooth, worker threads handle all the heavy decoding (stb_image for JPEG, libpng for PNG → Morton-tiled ABGR). The main thread just uploads the pre-decoded pixel buffer to the GPU, which takes about 10 ms.

- **Socket init**: `socInit()` (the 3DS network service) can only be called once per session. `network.c` calls it first, and the GPS server gracefully skips it if it's already initialized.

- **Tilepack format**: A 16-byte header ("3DTP", version, tile count, data offset), followed by a sorted index of 20-byte entries (zoom, x, y, offset, size), then concatenated raw tile data. The reader loads the full index into RAM and does binary search for O(log n) lookups, protected by a LightLock for thread safety.

- **QR encoder**: A custom ~335-line C implementation with no dependencies. Supports versions 1–4, byte mode, error correction level L. A typical GPS server URL is about 28 characters, which fits comfortably in version 2 (25×25 modules).

---

## License

This project is developed for educational and homebrew purposes. Map data is provided by OpenStreetMap contributors under the Open Database License.

## Disclaimer

This application requires an internet connection for live tile downloads. Data usage applies based on your WiFi connection. With offline tilepacks, the map works without any network access.
