# RC1 acceptance — 20 requested requirements

**Decision: RELEASE CANDIDATE ONLY. Stable publication is blocked.**

Starting source: `e9c0d135604b820c43c3646027628b7b5b88dd27`.
New branch: `release-candidate-production-20261111`.
Exact built source is recorded in `preview-builds/rc1/SOURCE_COMMIT`;
checksums and install QR sit beside both candidate binaries. The source and
artifact commits are separate so the build provenance is explicit.

PASS means the stated check was actually executed and its evidence exists.
UNVERIFIED means the requested end-to-end claim needs additional evidence; a
host PASS does not promote it. FAIL means a known part remains missing or blocked.

| # | Requirement | Status | Evidence and remaining work |
| --- | --- | --- | --- |
| 1 | Complete branch audit | UNVERIFIED | Verified starting head and both handoffs; traced networking/GPS, detail workers/renderer, controls, terrain/buildings, games, maps/offline/search/routes, tests and packaging. Fixed concrete integration issues. This is not certification that every line/provider edge case has been exhaustively reviewed. See inventory and provider review. |
| 2 | Fresh matching CIA and 3DSX | PASS | Clean devkitARM compilation, shader assembly, linking and CIA packaging; same source commit for both. Header/title-ID checks and artifact hashes recorded. Console execution is a separate gate. |
| 3 | Automated regression suites | PASS | Full host runner and integration gate; address/undefined sanitizers where supported, 30k challenge roundtrips, simulated classic/daily/multiplayer/retry games, terrain decode guard and pairing auth tests. Logs retained. LeakSanitizer is unavailable under this workspace's process tracing and is **not** claimed to pass. |
| 4 | Perfect panorama detail | UNVERIFIED | Existing zoom-2–4 planning/geometry/cancel/stale-identity tests pass; full ARM build links renderer. Single-central-tile limit remains. GPU appearance, transitions, original-3DS memory and permitted Google access unresolved. |
| 5 | Perfect detailed globe | UNVERIFIED | Geometry/LOD/worker tests; actual NASA request returned 256×256 image. Historical Blue Marble correctly distinguished from real-time imagery. One-patch seams/stereo/visual quality not hardware-tested. |
| 6 | Complete custom controls | FAIL | Four profiles/11-button bijection, persistence/corrupt recovery, START preservation and new hint translation tested. Important hints converted. Legacy printf/control captions remain; full touch/layout and all-profile console exercise incomplete. |
| 7 | Worldwide terrain | UNVERIFIED | Selected-coordinate math/parent-fallback/worker tests; real 256×256 tiles decoded for city/mountain/coast/flat/Tokyo samples on desktop. Added pre-decode dimension guard. Actual scene alignment, scaling and stereo unverified. |
| 8 | Stereo buildings | UNVERIFIED | Existing real OSM parser, extrusion/triangulation/bounds, worker errors and terrain alignment tested on host. Simple ways only; holes/complex roofs unsupported. Real console draw/performance and provider response acceptance unverified. |
| 9 | Street View reliability and physical gyro | UNVERIFIED | Existing navigation, heading, image, motion and cache host suites pass. No physical sensor, L/R intersection or real-console panorama tests performed. |
| 10 | Complete StreetGuess polish | UNVERIFIED | Existing World/US/Europe, rules, timers, scoring/roads/replay tests pass. Simulated complete games and no-round-loss exact-image retry pass. Geographic coverage/loading/visual polish on consoles unverified. |
| 11 | Full Daily Challenges | UNVERIFIED | Versioned, exact licensed Panoramax IDs; deterministic UTC-day challenges, difficulty, daily/weekly records and seven-day history implemented and host-tested in part. Dataset is only eight locations (France/US). UTC clock is console-supplied; real two-console equivalence and long-term save behavior unverified. |
| 12 | Friends and pass and play | UNVERIFIED | Dataset/seed/rule-bound checksummed codes, import path, 2–4-player state and shareable summaries added. 30k code tests and identical second-player round test pass. Real 3DS keyboard, 3/4-player end screens and photo readability unverified. |
| 13 | Compelling exploration modes | FAIL | Surprise Me, fixed city expedition and starter France/US Country Streak implemented. A broad Mystery Landmark Expedition collection, meaningful content progression and content balancing remain incomplete. No measured enjoyment/retention claims. |
| 14 | Secure phone GPS pairing | UNVERIFIED | Fresh per-session P-256 identity and independent 128-bit bearer token; unauthorized/duplicate submissions rejected; request URLs redacted; phone status hides exact position. Browser-verifiable trust onboarding, permissions/reconnect and universal screenshot privacy remain unresolved. See security review. |
| 15 | Maps/offline/search/routes | UNVERIFIED | Region/cache/resume/planning/concurrent append, QR, search inputs, GPS-follow, route progress/reroute debounce and stress suites pass. Poor Wi-Fi/SD/GPS and live console end-to-end behavior unverified. Full turn-by-turn not advertised. |
| 16 | Networking/licensing/reliability | FAIL | TLS peer/hostname checks and bundled CA roots implemented; live NASA/terrain/Panoramax host checks pass. Google/Bing authorization, app-scale Nominatim policy, provider attribution/access review and old curl dependency advisories remain production blockers. |
| 17 | Original-3DS optimization/endurance | UNVERIFIED | Bounded workers/detail allocation preserved, hostile terrain dimensions rejected, sanitizer math/runtime tests and route stress pass. No actual RAM/linear/VRAM/frame-time/SD/long-session measurements. No leak-free or crash-free hardware claim. |
| 18 | Hardware acceptance | UNVERIFIED | No physical original 3DS or New 3DS XL session performed. Azahar startup blocked by missing display backend. Hardware record intentionally contains no fabricated timings/screenshots. |
| 19 | Verified professional packages/CI | UNVERIFIED | Fresh candidate packages, preserved title ID, source asset inspection, checksums, commit-pinned downloads and decoded FBI QR are provided. CI now performs real checks/builds but remote CI execution, HOME Menu banner playback and physical FBI install remain unverified. Stable release not published. |
| 20 | README/full media release | FAIL | README redesigned; archived screenshot gallery, labeled hero/additional GIFs and 25-second teaser delivered. Inspected user clip contains ChatGPT conversation. Missing current-feature/physical footage and 2–4-minute full release demo are explicitly documented, not fabricated. |

## Evidence files

- `automated-tests.log`: actual full host run, including failures if any occurred.
- `build.log`: clean devkitARM build and CIA packaging output.
- `provider-smoke.json`: real desktop service checks; timings are not 3DS timings.
- `source-assets.json`: icon/banner/audio/CA source sizes, dimensions and hashes.
- `media-manifest.json`: delivered-media provenance, hashes and durations.
- `source-inventory.json`: source/module file paths, sizes and hashes.
- `hardware.md`: all physical checks explicitly UNVERIFIED, with acceptance procedure.
- `provider-review.md`: service-specific obligations and known production blockers.
- `media.md`: authentic source selection and unfinished release shot list.

## Actual test environment

Linux desktop host, GCC host C compiler with ASan/UBSan; LeakSanitizer cannot operate
under the workspace process tracer. `ASAN_OPTIONS=detect_leaks=0` was therefore
set explicitly for local runs. CI retains leak checking enabled on a normal runner.
No sanitizer errors are hidden or reclassified as passes.

devkitARM r68 / GCC 16.1.0, libctru 2.7.1, citro2d 1.7.0, citro3d 1.7.1,
3ds-curl 8.4.0, mbedTLS 2.28.8, libpng 1.6.53, Jansson 2.13 and zlib 1.3.1;
makerom 0.18.4 and bannertool 1.2.3. Compiler/package warnings remain visible in
the build log; an upstream ARM startup object warns about GNU-stack metadata.

No mandatory paid APIs, new hosted backend or new account was added. Some existing
optional providers have their own credentials/terms. No production release or
existing stable artifact is overwritten by this candidate.
