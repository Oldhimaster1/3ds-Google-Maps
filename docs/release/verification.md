# Preview verification

**On-device downloads for offline maps** is prepared for tag `v2.1.0-preview.1`. Both packages were built from [`59a356b`](https://github.com/Oldhimaster1/3ds-Google-Maps/commit/59a356bf7956003fc947969b4400a64b9a1dbeb9). The map runtime is from [`c587a06`](https://github.com/Oldhimaster1/3ds-Google-Maps/commit/c587a064068a8a79b1da85501dcf72e549ea0c9c). Clean rebuilds matched the tested `.3dsx` byte for byte.

| Check | Result |
| --- | --- |
| Build | Clean devkitARM build produced both packages. The region host regression suite passed. The compiler emitted existing warnings in the QR encoder and unused panorama handler. |
| Azahar package smoke tests | Azahar 2126.2 desktop frontend, configured as an Old 3DS with a separate virtual SD. The `.3dsx` launched and saved its view. The CIA installed as title `000400000FD34B00`, showed the approved icon in Azahar's installed-title list, launched from installed content, rendered the map, panned and saved the view. |
| Maps, search, and route | Loaded USGS maps and imagery through a controlled endpoint. Typed a City Hall Park search, panned Lower Manhattan, switched layers at the Golden Gate Bridge, and displayed a roughly 2 km OSRM route from The Battery. |
| Offline creation and resume | On each package, configured the tile source and named a 16-tile pack at zoom 14. Paused after 3 tiles, exited, and resumed in a new emulator process. The server log confirmed 13 missing tiles were requested. The in-app integrity check reported 16 readable tiles. |
| Cold offline reopen | Stopped the controlled tile server, confirmed the port no longer accepted requests, and launched a fresh emulator process with the same SD profile. The normal browsing cache contained zero files. The completed pack drew the map, panning changed the view within coverage, and **A** on the completed pack opened its preview. The test verifies the pack with the tile server stopped. |
| Direct USGS HTTPS | The candidate accepted the documented tile URL, but the live download saved 0 tiles in this Azahar environment. A separate diagnostic build reported curl error 6 (host name could not be resolved). A host-side request to the same service returned a JPEG. Direct service access on the console remains pending. |
| GPS | The emulator accepted a simulated HTTPS location fix and showed the reported accuracy. No physical phone browser was used. |
| Demo and README | The recut uses the same emulator footage. The 45-second MP4 and 11.5-second looping GIF decoded successfully. The GIF shows the completed pack, stopped-server card, and movement during the saved-pack pan. |
| Public packages and QR | Both commit-pinned downloads matched the tested files byte for byte. The QR decoded to the CIA URL at native and README display sizes. Downloading the target over TLS 1.2 returned the tested CIA. Physical FBI scanning and installation are pending. |
| Emulator attribution | The attribution is in the edited media. The app source, extracted CIA executable and title metadata, and the .3dsx executable contain no Azahar branding. |
| Physical Old 3DS | Pending. These captures do not establish hardware performance, real SD behavior, or phone compatibility. |
| HOME Menu and FBI | The CIA's embedded icon, banner and sound were extracted and verified. Display and sound playback on the physical HOME Menu and remote installation in FBI remain pending. |

Tested package SHA-256 values:

```text
0a0758b157168e5fc31b7b8f148e8f59b9b67a2cfbf5072001bed3acde2b87ac  3ds_google_maps.cia
fb33796f2d7ffbb89e81616f22ce6efc539f608797fc473c4cb7a2cf453973c0  3ds_google_maps.3dsx
```

## CIA branding and title metadata

- [`v1.0.0`](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v1.0.0) and [`v2.0`](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v2.0) each publish one `.3dsx`. Both downloads matched GitHub’s asset digests. Their 32-byte headers have no extended SMDH section. Neither package is a CIA or contains a HOME Menu banner sound.
- The [GameBrew mirror](https://www.gamebrew.org/3DS_Google_Maps) contains the identical v2.0 `.3dsx`, the v2.0 source tarball, and legacy tilepacks. The tarball contains the same `.3dsx` build recipe.
- The author selected the repository's root [`icon.png`](../../icon.png) (48 × 48) and [`banner.png`](../../banner.png) (256 × 128), introduced in [`8677367`](https://github.com/Oldhimaster1/3ds-Google-Maps/commit/8677367). The author approved creating an original HOME Menu chime in [`banner.wav`](../../banner.wav).
- The extracted SMDH icon and CGFX texture match the two PNGs at the console's respective RGB565 and RGBA4444 color depths. The extracted CWAV contains all 42,240 source WAV samples in mono 32 kHz PCM16, with looping disabled. These are checks of the embedded content.
- This is the first public CIA found in the release history. It uses title ID `000400000FD34B00`, product code `CTR-P-3MAP`, TMD version `2.1.0`, and the Old 3DS 64 MB application profile. Retain the title ID for future CIA upgrades.

## Demo credits

Topographic maps: [USGS The National Map](https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer?f=pjson). Imagery: [USDA and USGS The National Map](https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer?f=pjson). Both services reported export enabled. The offline test was limited to the Golden Gate area in the contiguous United States. Search and routing use data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), with Nominatim and OSRM/FOSSGIS serving the demo responses. The figures and edited footage carry these credits. Raw map tiles and tilepacks are excluded from the source footage package.
