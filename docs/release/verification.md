# Release candidate verification

This branch prepares **On-device downloads for offline maps**. The proposed tag is `v2.1.0-preview.1`. The candidate is built from the app and build files introduced by [`c587a06`](https://github.com/Oldhimaster1/3ds-Google-Maps/commit/c587a064068a8a79b1da85501dcf72e549ea0c9c). A clean rebuild after the documentation and media changes matched the tested binary byte for byte.

| Check | Result |
| --- | --- |
| Build | Clean devkitARM build produced `3ds_google_maps.3dsx`. The region host regression suite passed. The compiler emitted existing warnings in the QR encoder and unused panorama handler. |
| Candidate launch | Azahar 2126.2 desktop frontend, configured as an Old 3DS with a separate virtual SD. The exact candidate binary launched and saved its view on exit. |
| Maps, search, and route | Loaded USGS maps and imagery through a controlled endpoint. Typed a City Hall Park search, panned Lower Manhattan, switched layers at the Golden Gate Bridge, and displayed a roughly 2 km OSRM route from The Battery. |
| Offline creation and resume | Used the app UI to configure the source and name a 16-tile pack at zoom 14. Paused after 3 tiles, exited, and resumed in a new emulator process. The server log confirmed that the app requested only the 13 missing tiles. The in-app integrity check reported 16 readable tiles. |
| Cold offline reopen | Stopped the controlled tile server, confirmed the port no longer accepted requests, and launched a fresh emulator process with the same SD profile. The normal browsing cache contained zero files. The completed pack drew the map, panning changed the view within coverage, and **A** on the completed pack opened its preview. The test verifies the pack with the tile server stopped. |
| Direct USGS HTTPS | The candidate accepted the documented tile URL, but the live download saved 0 tiles in this Azahar environment. A separate diagnostic build reported curl error 6 (host name could not be resolved). A host-side request to the same service returned a JPEG. Direct service access on the console remains pending. |
| GPS | The emulator accepted a simulated HTTPS location fix and showed the reported accuracy. No physical phone browser was used. |
| Demo and README | GitHub's rendered README and styles fit 320, 390, and 1,280 px viewports, with four readable images and working install anchors. The hosted images, GIF, and MP4 matched the local exports. The 53.5-second re-downloaded H.264 MP4 played in Chromium. The 10.5-second GIF advanced and looped at phone width. The editable sources re-exported both files byte for byte. |
| Physical Old 3DS | Pending. These captures do not establish hardware performance, real SD behavior, or phone compatibility. |
| CIA, HOME Menu, and FBI | Blocked on the production package or its authoritative assets. CIA installation, HOME Menu icon and sound, FBI QR destination, and a hardware QR install remain unverified. |

Candidate `.3dsx` SHA-256:

```text
fb33796f2d7ffbb89e81616f22ce6efc539f608797fc473c4cb7a2cf453973c0  3ds_google_maps.3dsx
```

## Production branding audit

- [`v1.0.0`](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v1.0.0) and [`v2.0`](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v2.0) each publish one `.3dsx`. Both downloads matched GitHub’s asset digests. Their 32-byte headers have no extended SMDH section. Neither package is a CIA or contains a HOME Menu banner sound.
- The [GameBrew mirror](https://www.gamebrew.org/3DS_Google_Maps) contains the identical v2.0 `.3dsx`, the v2.0 source tarball, and legacy tilepacks. The tarball contains the same `.3dsx` build recipe.
- The `v2.0` source has no root icon or banner files. Its Makefile sets `NO_SMDH=1`. Commit [`8677367`](https://github.com/Oldhimaster1/3ds-Google-Maps/commit/8677367) later adds the current root `icon.png` (48 × 48) and `banner.png` (256 × 128). These are candidate artwork references, with no verified production CIA provenance.
- The reachable tags, packaging workflow branch, and historical source archive supplied no production `.cia`, compiled HOME Menu banner with CWAV audio, or authoritative title ID. The unmerged release workflow only builds and uploads a `.3dsx`.
- Resume packaging once the production CIA is available, or obtain the production icon, matching artwork, banner sound, and title ID/packaging settings together. Compare the final CIA’s extracted icon and banner audio to that reference. Treat an embedded audio match and hearing it on the HOME Menu as two separate checks.

## Demo credits

Topographic maps: [USGS The National Map](https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer?f=pjson). Imagery: [USDA and USGS The National Map](https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer?f=pjson). Both services reported export enabled. The offline test was limited to the Golden Gate area in the contiguous United States. Search and routing use data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), with Nominatim and OSRM/FOSSGIS serving the demo responses. The figures and edited footage carry these credits. Raw map tiles and tilepacks are excluded from the source footage package.
