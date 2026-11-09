[Download .cia](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/3ds_google_maps.cia) · [Download .3dsx](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/3ds_google_maps.3dsx) · [SHA256SUMS](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/SHA256SUMS) · [Watch the introduction](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/maps-for-nintendo-3ds-intro.mp4)

Save a named map region on the 3DS, pause and resume a download, and reopen the saved tiles offline. This preview adds region coverage previews, pack integrity checks, a restored map view, GPS accuracy, and cancellable place and route requests.

## Install

- **Homebrew Launcher** — Copy the `.3dsx` to `sdmc:/3ds/3ds_google_maps/3ds_google_maps.3dsx`.
- **HOME Menu** — On a 3DS with custom firmware, copy the `.cia` to the SD card and select **Install CIA** in FBI.

## Try an offline region

Open **Menu → Data → Offline regions**, press **L** to set a tile source that permits offline downloads, and press **Y** to plan a small pack. Use **A** to name and download it. **B** pauses, and **A** on the paused pack resumes. Once the pack is ready, reopen the app offline and press **A** on the pack to preview it. The pack covers the saved area and zooms. An existing route can define a corridor of tiles. Calculating a new route requires a connection.

## Compatibility

Both packages launched in Azahar 2126.2 in its Old 3DS configuration. The demo shows a small region saving, resuming and reopening with the controlled tile server stopped. A separate direct USGS request could not resolve the hostname in the test emulator. Physical Wi-Fi, phone browser pairing, HOME Menu playback and FBI remote install still need hardware verification. Some phone browsers may block the local GPS page's self-signed certificate.

[Installation and usage](https://github.com/Oldhimaster1/3ds-Google-Maps/blob/v2.1.0-preview.1/README.md) · [Verification record](https://github.com/Oldhimaster1/3ds-Google-Maps/blob/v2.1.0-preview.1/docs/release/verification.md) · [Report an issue](https://github.com/Oldhimaster1/3ds-Google-Maps/issues/new/choose) · [Stable v2.0](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v2.0)

For an issue report, include the console model, release version, tile provider, and steps to reproduce it.
