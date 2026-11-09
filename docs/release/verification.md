# Release candidate verification

This branch prepares **On-device downloads for offline maps**. The proposed tag is `v2.1.0-preview.1`. The candidate is built from the app and build files introduced by [`c587a06`](https://github.com/Oldhimaster1/3ds-Google-Maps/commit/c587a064068a8a79b1da85501dcf72e549ea0c9c). The subsequent release preparation commit adds documentation and demonstration media.

| Check | Result |
| --- | --- |
| Build | Clean devkitARM build produced `3ds_google_maps.3dsx`. The region host regression suite passed. The compiler emitted existing warnings in the QR encoder and unused panorama handler. |
| Candidate launch | Azahar 2126.2 desktop frontend, configured as an Old 3DS with a separate virtual SD. The exact candidate binary launched and saved its view on exit. |
| Maps, search, and route | Loaded USGS maps and imagery through a controlled endpoint. Typed a City Hall Park search, panned Lower Manhattan, switched layers at the Golden Gate Bridge, and displayed a roughly 2 km OSRM route from The Battery. |
| Offline creation and resume | Used the app UI to configure the source, name a 16-tile pack at zoom 14, pause, exit, and resume after a new emulator process launched. The server log confirmed that resume fetched only tiles missing from the partial pack. The in-app integrity check reported 16 readable tiles. |
| Cold offline reopen | Stopped the controlled tile server, confirmed the port no longer accepted requests, and launched a fresh emulator process with the same SD profile. The normal browsing cache contained zero files. The completed pack drew the map and panning changed the rendered view within coverage. This verifies use of the pack in Azahar. The emulator’s Wi-Fi status indicator was not used as evidence of connectivity. |
| GPS | The emulator accepted a simulated HTTPS location fix and showed the reported accuracy. No physical phone browser was used. |
| Demo and README | The 53.5-second H.264 MP4 decoded completely and played in Chromium. The 10.5-second GIF advanced and looped at phone width. A local GitHub-flavored Markdown render loaded all four images and fit 320, 390, and 1,280 px viewports. |
| Physical Old 3DS | Pending. These captures do not establish hardware performance, real SD behavior, or phone compatibility. |
| CIA, HOME Menu, and FBI | Blocked on the production package or its authoritative assets. CIA installation, HOME Menu icon and sound, FBI QR destination, and a hardware QR install remain unverified. |

Candidate `.3dsx` SHA-256:

```text
fb33796f2d7ffbb89e81616f22ce6efc539f608797fc473c4cb7a2cf453973c0  3ds_google_maps.3dsx
```

## Production branding audit

- [`v1.0.0`](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v1.0.0) and [`v2.0`](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v2.0) each publish one `.3dsx`. Both downloads matched GitHub’s asset digests. Their 32-byte headers have no extended SMDH section. Neither package is a CIA or contains a HOME Menu banner sound.
- The `v2.0` source has no root icon or banner files. Its Makefile sets `NO_SMDH=1`. Commit [`8677367`](https://github.com/Oldhimaster1/3ds-Google-Maps/commit/8677367) later adds the current root `icon.png` (48 × 48) and `banner.png` (256 × 128). These are candidate artwork references, with no verified production CIA provenance.
- The reachable tags, packaging workflow branch, and historical source archive supplied no production `.cia`, compiled HOME Menu banner with CWAV audio, or authoritative title ID. The unmerged release workflow only builds and uploads a `.3dsx`.
- Resume packaging once the production CIA is available, or obtain the production icon, matching artwork, banner sound, and title ID/packaging settings together. Compare the final CIA’s extracted icon and banner audio to that reference. Treat an embedded audio match and hearing it on the HOME Menu as two separate checks.

## Demo credits

Topographic maps: [USGS The National Map](https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer?f=pjson). Imagery: [USDA and USGS The National Map](https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer?f=pjson). Both services reported export enabled. The offline test was limited to the Golden Gate area in the contiguous United States. Search and routing use data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), with Nominatim and OSRM/FOSSGIS serving the demo responses. The figures and edited footage carry these credits. Raw map tiles and tilepacks are excluded from the source footage package.
