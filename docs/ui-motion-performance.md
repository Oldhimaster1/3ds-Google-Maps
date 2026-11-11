# StreetGuess UI, gyro and loading polish — development review

This branch addresses reported problems on the original and New 3DS. It is a
source candidate, **not a devkitPro/Azahar/physical-hardware verified build**.

## UI and accessibility

- New Settings > Text tab: Normal/Large/Extra Large, Regular/Medium/Bold
  simulation, and high-contrast secondary labels, persisted to
  `sdmc:/3ds_google_maps/ui_access.cfg`.
- The Bold mode draws a subtle second copy of the bundled system font. It is
  NOT a genuine separate bold font file.
- Explore settings cards use larger single-line headings instead of tiny
  subtitles, and the coverage footer is shorter.
- The Map center address card displays two natural-proportion text lines;
  other place-result rows are width-trimmed with ellipses instead of
  horizontally compressing characters.
- Long strings are measured before display and truncated on UTF-8 codepoint
  boundaries. Inspect international address strings and high-contrast layouts
  on a real console before release.

## StreetGuess

- The main menu now contains a short 3-step guide; Y toggles its display.
- During a round A focuses the bottom guessing map. Touch places/adjusts a pin,
  A again submits the selected guess. B closes focused guessing map; otherwise
  B pauses the round. The Guess action is labeled on the bottom screen.
- Translucent pause, result and map guidance overlays no longer draw IN FRONT
  of their text: text z=0.99; footer backgrounds sit behind text.
- The game temporarily disables coverage highlights when drawing guesses,
  restoring the prior setting immediately afterward.
- Starting StreetGuess drops stale ordinary map-tile downloads; the Google-only
  discovery session is kept separate from regular Street View preferences.
- Searches reuse a persistent HTTPS handle; the closest Google candidate is
  checked before downloading candidate JPEG images and no more than two
  candidates are decoded per coverage response. Fewer unnecessary repeated
  TLS handshakes may improve loading; **speedups have not been measured**.

## Street View and gyro

- Regular Street View D-pad: Left/Right turn, Up zooms in (FOV -5), Down zooms
  out (FOV +5), L/R move along connected panoramas, L+Down retries a search,
  Select re-centers/calibrates the gyroscope, X toggles gyro, Y switches source.
- The gyro now reads all three native axes and accelerometer. Yaw is projected
  onto measured gravity and pitch onto the console hinge axis orthogonal to
  gravity. The existing bias and filtering logic remains in place. Tests use
  synthetic flat/upright physical orientations.
- The panorama viewer reuses its TLS handle across searches, movements, and
  image-quality upgrades; all handles are freed after worker threads stop.
- Entry into Street View clears old map-tile queue work, while preserving
  cached tiles used by the fly-in. When the configured animation completes
  before imagery is ready, the map gently moves and displays a progress
  indicator rather than freezing on an unmoving final frame.
- The existing automatic 1024x512 zoom-1 Google imagery upgrade is retained.
  D-pad zoom changes the viewing field of view. A more complex zoom-dependent
  >1024 texture/tiled renderer was deliberately not added before hardware
  memory and texture performance testing.
- Map tiles already use two download workers, center-first queue priority
  and persistent curl handles. This update does not blindly add threads.

## Testing and release gate

```sh
sh tests/run-ui-access-host.sh
sh tests/run-pano-motion-host.sh
sh tests/run-guess-ui-host.sh
# Full project host suite:
for t in tests/run-*host.sh; do sh "$t"; done
git diff --check
```

In this development environment the two standalone tests above have actually
passed under host GCC, AddressSanitizer and UndefinedBehaviorSanitizer.
Updated simulated-UI tests and complete repository tests **still need to run
against a fresh checkout**, as do the devkitPro CIA/3DSX build and Azahar.
The real gyroscope complaint needs a physical 3DS rotation check despite
passing synthetic tests. Never describe these as completed.

On hardware, verify: exact two-line address display and all 9 combinations of
text size/weight; High contrast; real StreetGuess first-round tutorial, map
focus and guess submission, pause/result dialog text; normal Maps map-coverage
visibility after returning; D-pad Street View zoom, L/R navigation, gyro yaw
and pitch with console tilted; the animation during both fast/slow images;
worldwide StreetGuess successful-round rate, median and p95 loading latency;
map tile network/decode/upload timing; multi-round memory stability.

Do not replace the known working CIA, publish a release, or make an FBI QR
before a genuine cross-compiled CIA downloads with matching SHA-256 and
has undergone at least a runtime smoke test.
