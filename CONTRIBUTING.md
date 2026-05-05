# Contributing to 3DS Google Maps

Thanks for your interest in contributing! This is a Nintendo 3DS homebrew project written in C (C99), built with devkitARM. Please read this guide before opening issues or submitting pull requests.

---

## Table of Contents

- [Getting Started](#getting-started)
- [Building the Project](#building-the-project)
- [Project Structure](#project-structure)
- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Requesting Features](#requesting-features)
  - [Submitting Pull Requests](#submitting-pull-requests)
- [Code Style Guidelines](#code-style-guidelines)
- [Architecture Rules — Read Before Coding](#architecture-rules--read-before-coding)
- [Debugging on Device](#debugging-on-device)
- [Things We Won't Accept](#things-we-wont-accept)

---

## Getting Started

You'll need the devkitPro toolchain set up to build this project. There is no way to build it without it — it cross-compiles C to run on the 3DS's ARM11 processor.

1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS toolchain
2. Install the required libraries via `dkp-pacman`:
   ```bash
   dkp-pacman -S 3ds-dev
   dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-libpng
   ```
3. Fork and clone this repository
4. Confirm you can build successfully (see below) before making any changes

---

## Building the Project

### Linux / macOS

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

cd /path/to/3ds-Google-Maps
make
```

### Windows (devkitPro MSYS2 shell)

```bash
export DEVKITPRO=/c/devkitPro
export DEVKITARM=/c/devkitPro/devkitARM
export PATH=/c/devkitPro/devkitARM/bin:/c/devkitPro/tools/bin:$PATH

cd /c/path/to/3ds-Google-Maps
make
```

A successful build outputs `3ds_google_maps.3dsx`. Copy it to `sdmc:/3ds/3ds_google_maps/` on your SD card and launch from the Homebrew Launcher.

### Libraries linked

```
-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpng -lz -lcitro2d -lcitro3d -lctru -lm
```

All dependencies come from devkitPro's portlibs. Do **not** add third-party library paths to the Makefile.

---

## Project Structure

```
source/         ← All .c files (auto-compiled — just drop new files here)
include/        ← All headers
tools/          ← PC-side Python scripts (tile downloader, debug proxy)
Makefile        ← devkitARM build config
```

Key source files:

| File | What it does |
|------|-------------|
| `main.c` | Main loop, all UI, input handling, settings panel |
| `network.c` | Tile downloads (libcurl HTTPS), disk cache, tilepack lookup |
| `map_tiles.c` | Tile coordinate math, RAM cache, decode pipeline, rendering |
| `gps.c` | GPS mode dispatch and fix storage |
| `gps_server.c` | HTTPS GPS bridge server (mbedTLS, phone browser → 3DS) |
| `tilepack.c` | `.tilepack` reader with binary search index |
| `qrcode.c` | Self-contained QR encoder (no deps) |

---

## How to Contribute

### Reporting Bugs

Before opening a bug report:
- Check the [existing issues](../../issues) to see if it's already reported
- Pull `sdmc:/3ds_google_maps/log.txt` from your SD card — this is the **only** debug output on hardware and is essential for diagnosing issues

When opening a bug report, please include:
- Your 3DS model (Old 3DS / New 3DS / 2DS / etc.)
- Whether you're using WiFi for live tiles or a `.tilepack` for offline use
- The contents of `log.txt` from your SD card (or the relevant portion)
- Steps to reproduce the issue
- What you expected to happen vs. what actually happened

### Requesting Features

Open an issue with a clear description of what you'd like to see. Please check whether it's feasible given the hardware constraints — the Old 3DS has a 268 MHz ARM11 CPU and ~64 MB of RAM shared with the OS. Performance and memory impact matter here.

### Submitting Pull Requests

1. Open an issue first for any significant change so we can discuss the approach
2. Fork the repo and create a branch from `main`
3. Make your changes, following the guidelines below
4. Test on real hardware if at all possible — the emulator doesn't reflect real-world performance or WiFi behavior
5. Submit a pull request with a clear description of what changed and why

**Pull request checklist:**
- [ ] Builds successfully with `make` (no errors, no new warnings)
- [ ] Tested on hardware (or describe why you couldn't)
- [ ] `log.txt` shows no unexpected errors during the affected flows
- [ ] No new dependencies added outside of devkitPro portlibs
- [ ] New `.c` files are placed in `source/`, new headers in `include/`

---

## Code Style Guidelines

- **Language:** C99. No C++ features.
- **Indentation:** 4 spaces (no tabs)
- **Naming:** `snake_case` for functions and variables, `UPPER_SNAKE_CASE` for macros and enum values
- **Logging:** Use `log_write()` for anything that could fail on-device. The SD card log is the only practical debug channel on hardware. Use the established prefixes:
  - `[MAIN]` — startup, init, shutdown
  - `[NET]` — curl, tile downloads
  - `[TILES]` — cache hits/misses
  - `[GPS-SVR]` — GPS server events
  - `[GPS]` — GPS mode / fix
  - `[SETTINGS]` — settings panel taps
- **Error handling:** Always check 3DS `Result` codes using `R_FAILED(result)`
- **Memory:** Don't allocate large buffers on the stack. The Old 3DS has ~64 MB total RAM shared with the OS. Use `malloc`/`free`.
- **No JSON libraries:** The few API responses we parse (Nominatim, OSRM) use `strstr`/`sscanf`-style parsing. Keep it that way.

---

## Architecture Rules — Read Before Coding

These are hard constraints based on real hardware limitations. Please don't try to work around them.

### ❌ Never use OpenSSL
OpenSSL 3.x DRBG/PRNG fails to seed on 3DS hardware because it needs entropy sources that don't exist. It was tried and abandoned. Use **mbedTLS** (available as `3ds-mbedtls` in devkitPro). Do not open PRs that introduce OpenSSL.

### ❌ Never call `socInit()` twice without handling the duplicate
`socInit()` can only be called once per app session. `network.c` calls it first inside `net_curl_init()`. If you add code that also calls `socInit()`, you must check the return value and continue gracefully if it's already been initialized — don't return false or abort. See `gps_server.c` for the correct pattern.

### ❌ Never move JPEG/PNG decoding to the main thread
Satellite JPEG tiles take 1–2 seconds to decode on the Old 3DS. Decoding on the main thread causes visible UI freezes. All decoding runs on worker threads (see `map_tiles.c`). Keep it there.

### ❌ Never use `httpc` for HTTPS tile downloads
The built-in 3DS `httpc` service only supports TLS 1.0, which modern tile servers (OpenStreetMap, Esri) reject. All HTTPS downloads go through **libcurl** with the mbedTLS backend.

### ✅ Adding a new source file
Just create `source/myfile.c` and add a header to `include/`. The Makefile auto-compiles everything in `source/`. No Makefile edits needed.

### ✅ Adding a new settings control
1. Add `SETTINGS_HIT_MY_NEW_THING` to the `SettingsHitTarget` enum
2. Draw it in the appropriate tab render block in `render_settings_panel()`
3. Add a hit-test case in `get_settings_hit()`
4. Add a dispatch case in `apply_settings_hit()`

### ✅ Thread safety
The GPS server runs on a background thread. Any state shared between the GPS thread and the main thread **must** be protected with `LightLock`. Never read/write shared GPS state without holding the lock.

### ✅ citro2d drawing
All citro2d draw calls must happen inside a frame (`C3D_FrameBegin` / `C3D_FrameEnd`). Never draw outside the frame.

---

## Debugging on Device

The 3DS has no console output visible during normal use. All debug information is written to:

```
sdmc:/3ds_google_maps/log.txt
```

This file is overwritten each time the app launches. When something goes wrong on hardware, **pull this file first** — it logs every key operation and its result. If you're submitting a bug report or PR for a hardware-reproducible issue, include the relevant portion of this log.

---

## Things We Won't Accept

- OpenSSL as a dependency (see above — it does not work on 3DS)
- New third-party libraries outside of devkitPro portlibs
- Changes that break the build with a clean `make`
- Tile sources that violate the provider's usage policy (e.g. bulk-downloading OpenStreetMap tiles)
- Regenerating `include/gps_server_cert.h` — the embedded TLS cert is valid until 2036 and does not need to be replaced

---

## Questions?

Open an issue and tag it with the `question` label. We're happy to help you get your dev environment set up or talk through an idea before you start coding.
