# 3DS Google Maps

Interactive map viewer for the Nintendo 3DS. Pulls OpenStreetMap and Esri satellite tiles over HTTPS, does GPS tracking through your phone, has place search, route planning, favorites, and a settings UI. Everything runs directly on the handheld, no proxy needed.

Tile downloads use libcurl with mbedTLS for TLS 1.2 (the 3DS's built-in HTTP service only does TLS 1.0, which most tile servers reject now). Satellite tiles are JPEGs that take 1-2 seconds to decode on the old 3DS CPU, so that work happens on background threads to keep the map from freezing.

v2.0 added offline tile caching. You can download a whole region on your PC, pack it into a single file, and copy it to the SD card. The map then works without WiFi.

---

## Features

**Map display.** Top screen is the map, bottom screen is controls. Touch drag to pan, D-pad or Circle Pad also work. L/R to zoom (levels 1-18). There's a crosshair at center, optional night mode, and a debug overlay if you want to see zoom level and cache stats.

**Tile sources.** Three options: OpenStreetMap streets (decoded with libpng), Esri satellite imagery (decoded with stb_image on worker threads), and Esri World Street Map. Switch between them in settings. Every tile you view gets cached to the SD card, and the 64 most recent stay in RAM.

**Offline tilepacks.** Run `tools/download_region.py` on your PC to bulk-download tiles, then copy one `.tilepack` file to the SD card. Way faster than transferring thousands of individual PNGs over FAT32. The app also has a "Download Region" button in settings if you want to grab tiles for your current view directly on the 3DS, though the PC tool is better for large areas.

**GPS via phone.** The 3DS runs a TLS server on port 8443. Open the URL on your phone's browser, grant location permission, and your GPS coordinates stream to the 3DS every second. A QR code shows on the top screen so you don't have to type anything. There's also an NMEA TCP client mode if you use an app like GPS2IP.

**Search and routing.** Place search via Nominatim, route planning via OSRM, reverse geocoding ("Identify" button tells you what's at map center). You can save favorite locations. There's basic panorama support too, but it needs the debug proxy running.

---

## Controls

| Input | Action |
|-------|--------|
| D-Pad | Pan the map |
| Circle Pad | Smooth analog pan |
| X / Y | Zoom in / out |
| Touch drag | Pan the map |
| Bottom panel buttons | Search, Favorites, Recents, Route, GPS, Track, Menu |
| A | Toggle GPS tracking |
| B | Close settings / go back |
| START | Exit the app |

---

## Offline map caching

If you want to use the map without WiFi, or just want tiles to load instantly, you can pre-download a region on your PC.

### Quick start

You need Python 3. Then:

```bash
# Satellite tiles, 10 km around NYC, zoom 10-14
python tools/download_region.py --lat 40.7128 --lon -74.006 --radius-km 10 --zoom 10-14

# Bigger region, more workers, skip the confirmation prompt
python tools/download_region.py --lat 50.0 --lon -80.0 --radius-km 200 --zoom 6-12 -w 16 -y
```

Output goes to `./3ds_google_maps/tiles/` by default, matching the SD card layout. Copy it over:

```
SD card:
  sdmc:/3ds_google_maps/tiles/sat.tilepack      ← satellite tiles
  sdmc:/3ds_google_maps/tiles/street.tilepack    ← street map tiles
```

The app finds these on startup. Re-run the script for more areas and it merges new tiles into the existing pack.

### Why one file instead of individual tiles?

Copying 15,000 small PNGs to a FAT32 SD card takes over an hour. A single `.tilepack` file transfers in under a minute. The app reads it with a binary search index, so lookups are O(log n).

### Deduplication

The download tool hashes each tile with SHA-256 and only stores unique ones. Ocean tiles, empty land, etc. are all identical, so for coastal regions this cuts file size by 20-50%.

### Why Esri and not OpenStreetMap?

OpenStreetMap's tile usage policy doesn't allow bulk downloading. The download tool uses Esri servers (World Imagery for satellite, World Street Map for streets). Normal interactive browsing on the 3DS still uses OSM tiles, which is fine under their policy.

---

## GPS phone bridge

The 3DS has no GPS, but your phone does.

1. Open Settings, go to the GPS tab, tap **Server**
2. A QR code appears on the top screen
3. Scan it with your phone, it opens a webpage
4. Allow location permission when the browser asks
5. Your phone starts sending coordinates to the 3DS every second
6. The map follows your position in real time

The certificate is self-signed (compiled into the binary), so your browser will show a security warning. Tap through it ("Advanced", then "Proceed").

Both devices need to be on the same WiFi network.

**iPhone users:** Safari has been unreliable with the self-signed cert and geolocation on this setup. Use Google Chrome on your iPhone instead, it works much better.

---

## Building from source

### Prerequisites

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS toolchain, then the required libraries:

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

On Windows (devkitPro at `C:\devkitPro`):

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

1. Copy `3ds_google_maps.3dsx` to `sdmc:/3ds/3ds_google_maps/` on the SD card
2. (Optional) Copy `.tilepack` files to `sdmc:/3ds_google_maps/tiles/`
3. Launch from the Homebrew Launcher
4. WiFi is needed for live tile downloads and GPS, but tilepacks work offline

### SD card paths

```
sdmc:/3ds_google_maps/
  tiles/                 — cached tiles (auto-created)
  tiles/sat.tilepack     — offline satellite pack (optional)
  tiles/street.tilepack  — offline street pack (optional)
  favorites.txt          — saved locations
  log.txt                — debug log (overwritten each launch)
```

---

## File structure

```
source/
  main.c          — main loop, UI, settings, input, offline download modal
  network.c       — tile downloads (libcurl HTTPS, httpc HTTP fallback), disk cache, tilepack lookup
  map_tiles.c     — tile coordinate math, RAM cache, decode pipeline, prefetch, rendering
  gps.c           — GPS mode dispatch (NMEA client vs HTTPS server)
  gps_server.c    — HTTPS GPS server (mbedTLS, serves HTML page, receives POST /location)
  tilepack.c      — .tilepack reader, binary search index, thread-safe reads
  qrcode.c        — QR encoder, byte mode, EC level L, versions 1-4
  simple_png.c    — PNG loader via libpng, outputs Morton-tiled ABGR for GPU
  logging.c       — writes to sdmc:/3ds_google_maps/log.txt
  stb_image.c     — stb_image build unit for JPEG decode

include/
  tilepack.h        — tilepack format spec and read API
  network.h         — download API, TileSource enum
  map_tiles.h       — tile cache and rendering API
  gps.h             — GPS init/cleanup/fix API
  gps_server.h      — GPS server API
  gps_server_cert.h — embedded DER cert + RSA-2048 key (valid 2026-2036)
  qrcode.h          — qr_encode()
  logging.h         — log_write()
  simple_png.h      — PNG loader
  stb_image_all.h   — stb_image v2.30
  stb_image.h       — stb_image public header

tools/
  download_region.py — bulk tile downloader, outputs .tilepack (run on PC)
  tile_proxy.py      — debug proxy for tile requests (dev only)
  generate_cert.py   — generated the embedded TLS cert (already done, don't re-run)

Makefile             — devkitARM build config
```

---

## Technical notes

The 3DS's built-in `httpc` only speaks TLS 1.0, which modern tile servers reject. libcurl with mbedTLS handles TLS 1.2. OpenSSL was tried early on but its PRNG can't seed on 3DS hardware (there's no entropy source it recognizes), so mbedTLS is the only option. It uses `sslcGenerateRandomData()` from the 3DS SSL system service.

Satellite JPEG tiles take 1-2 seconds to decode on the old 3DS's 268 MHz ARM11. Worker threads handle all the decoding (stb_image for JPEG, libpng for PNG, both outputting Morton-tiled ABGR). The main thread only does the GPU upload, which takes about 10 ms.

`socInit()` (the 3DS network service) can only be called once per session. `network.c` calls it first. The GPS server tries to call it too, but if it fails (because it's already initialized), it just continues. This is intentional.

The tilepack format is straightforward: 16-byte header ("3DTP", version, tile count, data offset), then a sorted index of 20-byte entries (zoom, x, y, offset, size), then the raw tile data concatenated. The reader loads the full index into RAM and does binary search. A LightLock protects the file reads so multiple threads can look up tiles concurrently.

---

## License

Built for educational and homebrew purposes. Map data from OpenStreetMap contributors under the Open Database License.

## Disclaimer

Live tile downloads need an internet connection. With offline tilepacks, the map works without any network access.
