# RC1 hardware and emulator acceptance record

**Actual result: UNVERIFIED. No physical acceptance session was performed.**
Old 3DS and New 3DS XL frame rates, loading times, heap/linear memory, VRAM,
stereo disparity, battery impact and stability are **not measured**. They are not
zero and must not be inferred from desktop tests or old media.

Azahar 2126.2 was downloaded and extracted. Startup failed before loading the
application because a usable graphical backend/display is unavailable in this
workspace. An offscreen attempt reported no offscreen platform plugin; only xcb
and Wayland plugins are supplied. No RC1 CIA install, launch or screenshot was
obtained. Historical Azahar evidence in `../verification.md` belongs to an older
preview and is not reused as RC1 acceptance.

## Required acceptance session — repeat on each console

Record console model, firmware/homebrew environment, source commit, binary hashes,
SD type/free space and network configuration. Exclude private GPS URLs/coordinates
from public recordings. Record failures as failures; do not fill empty rows with
assumed results.

| Check | Old 3DS | New 3DS XL | Evidence to record |
| --- | --- | --- | --- |
| CIA via FBI QR; HBL 3DSX; icon/banner/audio | UNVERIFIED | UNVERIFIED | Install/launch video, source hash, title ID |
| Maps/search/coordinates/favorites | UNVERIFIED | UNVERIFIED | Real requests, input formats, restart/persistence |
| Offline creation/pause/resume/reopen | UNVERIFIED | UNVERIFIED | Missing-only requests, cold launch without Wi-Fi, low SD space |
| Panorama detail zoom 2–4 | UNVERIFIED | UNVERIFIED | Fast vs Detail at identical view; rapid ID changes while loading |
| Panorama navigation / intersections / backtracking | UNVERIFIED | UNVERIFIED | Actual different IDs/images, failed-request retention |
| Gyro | UNVERIFIED | UNVERIFIED | Physical yaw/pitch, calibration, 60s stillness, horizon, sensitivity/inversion |
| Globe and 3D slider | UNVERIFIED | UNVERIFIED | Online/offline texture, orientation, left/right alignment, seams |
| Terrain and buildings | UNVERIFIED | UNVERIFIED | City, mountain, coast, flat land, missing tile, real footprint alignment |
| Controls | UNVERIFIED | UNVERIFIED | All 11 keys × 4 profiles, conflict swap, restart, corrupt config, reset, fixed START |
| Classic StreetGuess | UNVERIFIED | UNVERIFIED | World/US/Europe × Moving/No Move/NMPZ; timer and failed lookup |
| Daily / friend / local multiplayer | UNVERIFIED | UNVERIFIED | Same UTC date/code yields same IDs/rules; 2–4 players; retry without loss |
| Starter discovery / Country Streak | UNVERIFIED | UNVERIFIED | Correct/incorrect input, clean end/replay, no accidental classic scoring |
| Phone GPS | UNVERIFIED | UNVERIFIED | Real QR scan, certificate trust, permission, auth rejection, speed/heading, reconnect |
| Routing / GPS-follow | UNVERIFIED | UNVERIFIED | Accuracy/jitter, ETA/progress, off-route debounce and recovery, cancellation |
| Endurance | UNVERIFIED | UNVERIFIED | 60+ min, rapid rotate/zoom/mode changes, Wi-Fi loss, low storage, corrupt saves |

During endurance testing record FPS ranges and worst stalls rather than a single
best frame rate. Record linear memory before/after at least 20 mode cycles, initial
and settled loading time, peak heap/VRAM if instrumentation permits, and any
freeze/crash/recovery steps. Use identical public test coordinates across consoles.

Only physical slider viewing or a stereo capture can support a stereoscopic
appearance claim. A normal flat-screen video cannot demonstrate perceived depth.
