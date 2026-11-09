# Draft release package

- Proposed tag `v2.1.0-preview.1`
- Release title **On-device downloads for offline maps**
- Publication depends on verifying the production CIA branding and title metadata.

## Release body

[Download .cia](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/3ds_google_maps.cia) · [Download .3dsx](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/3ds_google_maps.3dsx) · [SHA256SUMS](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/SHA256SUMS) · [Full introduction video](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/maps-for-nintendo-3ds-intro.mp4)

This preview adds named offline map packs on the console. Pick an area or the corridor around an existing route, pause and resume the download, then reopen the saved tiles without the tile server. It also adds coverage previews, a pack integrity check, a restored map view, GPS accuracy, and cancellable place and route requests.

- **Homebrew Launcher:** copy the `.3dsx` to `sdmc:/3ds/3ds_google_maps/3ds_google_maps.3dsx`.
- **HOME Menu:** use a custom firmware setup with a CIA installer such as FBI. Install the `.cia` from the SD card. Follow the tagged README for the direct download and any verified FBI QR.
- **Try offline maps:** open **Menu → Data → Offline regions**, use **L** to set an offline-capable tile source for the current layer, then **Y** to plan a small pack. A connection is needed for the download and new routes. Saved maps cover the chosen area and zooms.

The demo was recorded in Azahar using a controlled tile endpoint. Direct service requests and phone browser pairing still need physical testing. The GPS page uses a self-signed certificate, which some browser setups may block. Report your model, release version, tile provider, and reproduction steps in [Issues](https://github.com/Oldhimaster1/3ds-Google-Maps/issues/new/choose).

## Before publishing

1. Recover and identify the production CIA or the production icon, banner model/artwork, banner sound and title ID. Compare the built CIA against those references and record installation, HOME Menu display and audio results separately.
2. Set the README download links to the chosen tag and remove the hold notice after the asset checks. Generate the FBI QR from the actual tagged CIA URL only after confirming it is suitable for FBI. Keep the existing stable v2.0 link labeled separately.
3. Build both packages from the same final commit. Test and hash the exact release files. Attach the two packages, `SHA256SUMS`, and `maps-for-nintendo-3ds-intro.mp4` to the preview release.
4. Verify each public download against its hash, then bring the branch README to the repository default branch through a PR. Keep the raw captures in the separate source package.

## Listing and announcement copy

### Suggested repository description

Street maps, satellite imagery, place search, route planning, offline regions, and phone GPS for Nintendo 3DS homebrew.

### Suggested topics

`nintendo-3ds`, `3ds-homebrew`, `maps`, `offline-maps`, `openstreetmap`, `gps`

### Universal-DB

Maps for Nintendo 3DS displays street maps and satellite imagery, searches for places, and plans routes. Save named offline map regions directly on the console using a compatible source, or pair a phone on the local network for GPS. Install the CIA to the HOME Menu with custom firmware, or run the .3dsx from Homebrew Launcher. Physical Old 3DS testing is pending for this preview. [Project and releases](https://github.com/Oldhimaster1/3ds-Google-Maps)

### GameBrew

Maps for Nintendo 3DS is a dual-screen homebrew map viewer with street and satellite layers, search, routing, favorites, and phone GPS. The offline region manager saves named packs for selected areas and zoom levels, checks their tiles, and resumes interrupted transfers. New search and route requests require internet access. For offline use, configure a tile provider that permits downloads. The demo shows the workflow in Azahar. [Install, controls, and source](https://github.com/Oldhimaster1/3ds-Google-Maps)

### Community announcement, after publishing

See a map region download to the 3DS and reopen after the tile server stops. [Watch the demo](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/download/v2.1.0-preview.1/maps-for-nintendo-3ds-intro.mp4). The preview adds named offline packs, pause and resume, coverage checks, and phone GPS accuracy. [Download or read the setup guide](https://github.com/Oldhimaster1/3ds-Google-Maps). If you try it on physical hardware, please share your console model and what worked.
