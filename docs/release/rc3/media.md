# RC3 promotional media — review draft

## Current edit

- **Hero:** roughly 9 seconds, five shots, looping. Globe rotation, a closer Manhattan map view, StreetGuess pass-and-play menu, map zoom and globe movement.
- **Teaser:** 20.3 seconds, ten shots. Direct opening, short cuts, enlarged main display with the corresponding touchscreen alongside it, and a brief preview-download invitation.
- **Showcase:** 32 seconds, ten shots. Longer views of map exploration and globe movement, plus daily/friend, local-play and Mystery Expedition menus. It remains a preview showcase, not a complete gameplay demonstration.
- **Gallery:** lower Manhattan close-up, globe with Africa/Europe, daily challenge menu and two-player setup menu. The last two are explicitly menu captures.

All application imagery is genuine RC3 rendering. Cuts are at normal speed;
there are no invented app screens, generated panoramas, interpolated frames or
fabricated guesses/scores. The two screen crops in each video come from the
same source frame. Videos are silent. Hero frames retain the original stacked
screens with short editorial captions outside the capture.

Source: `9e09e27a12d88dee8de5aca5983f5d4a245b5999`; unchanged preview packages:
`9cc4858b5fca0cb8d3806ead7ed0b960c7f144fb`.

[Edit decisions, source hashes and timings](promo-edit.json) ·
[Output sizes and SHA-256 hashes](media-manifest.json) ·
[Editing script](../../../tools/edit_rc3_promo.py)

## Capture provenance

The recovered original recordings supplied globe movement, a wider map view
and StreetGuess menu selection. A new capture launched the exact public RC3
3DSX (SHA-256 `8e64eeb6980bb86f80e24ae37f8a0378ffc17d070ec63f1209b9710ba3de9a72`)
and recorded closer Manhattan pan/zoom. Genuine Esri image tiles were prepared
in the emulator's normal browsing cache from their original tile URLs before
recording. This is cached rendering, not a demonstration of live downloading or
offline-region installation. Incomplete-tile shots were removed from the edit.

The original captures and new city capture use Azahar/Xvfb, software OpenGL,
and a 400 × 480 dual-screen region. The source recordings run at 30 fps;
the GIF samples them at 12 fps. No archive build footage is mixed in.

Credits: NASA Blue Marble for the globe; Esri and contributors for satellite
imagery. These credits also appear in the videos and hero. StreetGuess screens
are menus; no third-party panorama appears in this edit.

## Recording environment and unsuccessful attempts

The unchanged candidate again failed direct DNS lookup in this environment.
An existing diagnostic build differs only by adding the local recording
proxy CA to its trust bundle. A host resolver/CONNECT shim was tried with it,
keeping TLS validation enabled. The challenge attempt still ended at the exact
image retry screen. Neither diagnostic footage nor error screens are included
in any delivered promotional asset. No diagnostic binary, CA or networking
change is shipped in this commit. This work does not establish real-console
network connectivity, gyro, stereo or performance.

## Still needed for the final release showcase

- Enter Street View and look around a real, identifiable panorama.
- Move to another available viewpoint.
- Show the same scene before and after supported panorama detail.
- Place a StreetGuess pin and reveal the genuine score/result.
- Capture daily/challenge-code gameplay beyond the menus.
- Show live terrain and buildings clearly.
- Record original/New 3DS controls, gyro, stereo and performance on hardware.

These remain capture gaps; the current assets do not imply they were completed.
No stable publication, tag, main merge or announcement has been made.
