# 2.2.0 RC1 — integration and shared challenges

**Experimental candidate, not production-cleared.** Starts at
`e9c0d135604b820c43c3646027628b7b5b88dd27` on
`feature-live-detail-and-remap-20261111`. Development lives on the separate
`release-candidate-production-20261111` branch. Existing stable and preview builds
are retained. The final source commit is recorded beside the binaries in
`preview-builds/rc1/SOURCE_COMMIT`; both binaries are built from that source.

## Added

- Versioned eight-location Panoramax challenge dataset with exact IDs, STAC URLs,
  creator credits, explicit image-license identifiers and independently decoded SD
  image hashes. No imagery is bundled or written to an SD imagery cache.
- Five-round shared daily challenges based on UTC day and dataset version;
  difficulty-specific daily bests, current-week totals, best retained week, recent
  seven-day history and an optional-to-view streak. Local history retains up to
  180 difficulty/day records. The console UTC clock must be correct.
- Checksummed friend codes that encode dataset identity, seed and difficulty;
  invalid, altered and incompatible codes fail validation. Codes are reproducible,
  not cryptographic anti-cheat or a signed online leaderboard.
- Local two-to-four-player pass and play, with handoffs, identical five-round
  locations and photograph-friendly score/code summaries.
- Surprise Me, a repeatable city discovery route and a starter France/US Country
  Streak mode. This is **not a completed mystery-landmark expedition library**.
- Three difficulty rules: Explorer (hints/unlimited), Standard (No Move/120s),
  Expert (NMPZ/60s). Country Streak uses country entry without a timer.

## Fixed and hardened

- Fresh-checkout StreetGuess UI tests now use the current panorama structure and
  exercise the detail integration. Test SD state is isolated per run so prior
  replay history cannot pollute tests.
- Exact daily/friend rounds intentionally bypass the random game's previous-game
  repeat filter. Missing images stop for retry without consuming a round or
  switching locations. Daily records do not enter classic random-game records.
- All curl provider paths use peer/hostname verification and bundled CA roots;
  legacy HTTP redirects to HTTPS enter verified curl, and HTTPS redirects cannot
  downgrade to HTTP. Request URLs no longer appear in network logs.
- Phone GPS uses independent per-session random pairing secrets and fresh P-256
  certificate identities, authenticated location POSTs, strict duplicate-header
  rejection and no permissive CORS response. The phone status omits exact
  coordinates; stalled sends have a six-second abort timeout.
- Terrain dimensions are inspected before decoding to reject oversized images
  without allocating a giant decoded buffer.
- Important Explore and StreetGuess control hints now show remapped buttons;
  configuration parsing rejects trailing corruption. Legacy captions still need
  a complete conversion.
- ARM-specific fixed-width record formatting and QR encoder bounds were corrected.
- Asset regression no longer requires intentionally removed demo panoramas.
- CI replaces the placeholder echo with full host checks, a devkitPro CIA/3DSX
  build, title-ID/package inspection, checksums and retained artifacts. No automatic
  public-release job is added.

## Preserved from the starting branch

Existing Google zoom-2–4 curved central detail tile, on-demand NASA globe patch,
stereo renderer, selected-location terrain and optional OSM buildings, four remap
profiles, multi-provider Street View, gyro/fly-in logic, classic regional/rule/timer
StreetGuess, phone GPS, offline packs, search formats, favorites and route-follow
progress/rerouting. Their real-console behavior is not established by preserving
or compiling their source.

## Known limits and blockers

- Original 3DS / New 3DS XL acceptance and physical gyro/stereo tests are unperformed.
- Azahar cannot initialize a graphical backend in this workspace; RC1 was not run.
- Existing unofficial Google/Bing integrations have unresolved production access
  permission. Public service usage and Nominatim aggregate traffic need resolution.
- devkitPro's supplied curl 8.4.0 dependency requires an advisory/upgrade review;
  this is not a security-certified release.
- Self-signed local GPS browser trust and geolocation behavior need physical phone
  testing and a browser-verifiable identity onboarding design.
- Panorama and globe detail each retain a single patch; no full-view detail mosaic.
- Terrain uses one tile, with bounded parent fallback. Complex OSM multipolygons,
  holes and roof types are not supported. No fake buildings are substituted.
- Some control instructions remain logical/default labels.
- Shared dataset coverage is eight city panoramas, mostly France. A richer,
  geographically balanced and landmark-specific content library is unfinished.
- No universal privacy-redaction screenshot mode or cheat-proof results.
- No tested full turn-by-turn instructions, voice guidance or offline route calculation.
- New physical footage and current-feature screenshots are unavailable. New GIFs
  and the 25-second teaser are explicitly labeled archived emulator footage.
- No 2–4-minute full RC1 release demo was fabricated from absent footage.

See [20-item acceptance report](acceptance.md), [provider review](provider-review.md)
and [hardware checklist](hardware.md). Resolve blockers before stable publication.
