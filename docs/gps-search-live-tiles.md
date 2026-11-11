# GPS pairing, search and live-map tiles — development branch

Branch: `gps-search-live-tiles-fix`. This branch starts from
`streetguess-ui-motion-polish`; it does **not** modify main or a working CIA.
Changes are actual source files, not an unapplied patch.

## Changes

* QR: corrected both format-information copies in `source/qrcode.c`, and
  cleared the top-screen QR's four-module quiet zone. Tests include a
  second BCH implementation plus OpenCV decoding under mild simulated camera
  blur, rotation and JPEG compression at 400x240.
* Phone GPS bridge: fixed truncated 204/404/OPTIONS HTTP responses using
  `sizeof(response)-1`. Reject invalid location bodies with HTTP 400.
  The page counts **acknowledged** fixes and limits updates to one in-flight
  request / roughly 1.5 seconds. The app reports fix count, age and accuracy.
  The phone and 3DS must be on mutually reachable Wi-Fi, and mobile browsers
  may warn about the bridge's existing self-signed HTTPS certificate.
* Search: native keyboard restores the last query (for the current session);
  ASCII whitespace trim keeps UTF-8 intact; coordinate entry remains intact.
  There is no extra area-selection prompt. X on a multi-result panel explicitly
  retries worldwide. Touching a result selects it. Address/city can render on
  two proportional lines. A short-lived result cache avoids duplicate API
  requests, and a nonblocking cooldown prevents rapid calls to the public
  Nominatim server. **There is no remote per-keystroke autocomplete.**
* GPS live tiles: `TileViewport` scores visible tiles and rejects outdated
  queued requests; the main thread sorts the bounded request queue after map
  tile boundary changes. The worker keeps its existing two persistent
  connections and in-flight requests. A zoom/accuracy-aware GPS movement gate
  avoids churning the queue for small stationary GPS jitter.
* Offline pack downloading, StreetGuess, terrain, and panorama networking
  are otherwise unchanged.

## Host regression tests

```sh
sh tests/run-gps-search-live-host.sh
```

The independent compiled QR scan requires host Python packages `numpy`
and `opencv-python` (cv2). Existing multisource tests additionally need
host Jansson development headers. Address, GPS, queue and QR helpers use
address/undefined behavior sanitizers where supported.

## Still required before calling this hardware-tested

1. Run full devkitPro/3ds-dev build to produce CIA and 3DSX, check build logs.
2. Launch the CIA in Azahar: confirm GPS settings and search panels fit on the
   bottom screen, and QR modules occupy the top screen with a clear border.
3. On physical Old 3DS, use a phone to scan the generated QR and confirm the
   URL bytes and accepted GPS fix count. Repeat on a New 3DS if possible.
4. Compare GPS-follow live-map loading at the same places and zoom levels,
   cold versus warm cache, before and after. Log p50/p95 visible tile latency,
   queue depth, network/decode/SD/upload times and FPS. Check wifi dropouts,
   rapid movement, map layer changes and GPS disconnects.
5. Check that the source results match real search queries, long addresses,
   exact coordinates, local results, worldwide retry and query recall.
6. Install only after reviewing physical test findings. Generate FBI Remote
   Install QR **only** from an uploaded CIA verified byte-for-byte.

## Boundaries

No assumption is made that a syntactically valid QR eliminates self-signed
certificate warnings. No assumption is made that a source-level optimization
produces a specific percentage speedup without device measurements. The
public Nominatim endpoint does not permit client-side autocomplete; its
published usage policy must be honored. All HTTP tile sources have their own
interactive-request/caching rules.
