# RC3 media — review draft

## New captures

- `docs/media/rc3/hero.gif`: 8 seconds of globe rotation/zoom, captured from RC3.
- `docs/media/rc3/teaser.mp4`: 28-second review teaser, 1280 × 720, silent.
- `docs/media/rc3/review-walkthrough.mp4`: 62 seconds, 1280 × 720, silent.
- `map-cached.png`, `globe.png`, `challenges.png`: fresh RC3 screenshots.

The walkthrough contains 18 seconds of map pan/zoom and settings, 24 seconds of
globe rotation/zoom and 20 seconds of StreetGuess menu selection. The teaser uses
6, 16 and 6 seconds from those segments. Both display RC3/Azahar attribution and
identify the cached map and menu-only limits. No archive gameplay is mixed in.
Globe credit: NASA Blue Marble. Satellite credit: Esri and contributors.

The captures use the unchanged uploaded RC3 source/binary, not the temporary
certificate-adjusted diagnostic build. They demonstrate actual UI input and
rendering. They do not demonstrate live NASA detail, live imagery fetching,
physical gyro, stereo depth or hardware frame rate.

## Recording environment

The original profile and virtual SD were recovered. Xvfb at 1280 × 960,
llvmpipe/OpenGL, 400 × 480 dual-screen crop; normal-speed 30 fps capture.
Old workers were found sharing command files, so the final worker uses unique
paths/display. The original working globe clip was retained.

Direct emulator DNS is blocked in this environment. A host-side resolver/CONNECT
shim made outbound connections possible, but the unchanged candidate correctly
rejected the environment proxy certificate. A separate diagnostic build added the
local test CA to its bundled trust store, without disabling TLS validation.
Panoramax rounds still failed to load within the request deadlines. This build
was not uploaded and is not represented as the RC3 download.

## Still needed before the full release video

Live Street View and navigation; the same view before/after panorama detail;
live terrain and buildings; a completed StreetGuess round; Country Streak record
reload on a console; and real-device control/gyro footage. The short review
walkthrough is not a substitute for the full gameplay demonstration.
