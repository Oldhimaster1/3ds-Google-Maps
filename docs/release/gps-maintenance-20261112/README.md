# GPS maintenance candidate — November 12, 2026

**NOT a confirmed Safari fix. Do not replace release assets yet.**

Starting branch: `fix-gps-https-browser-compat-20261112`.
Starting commit: `e7791340677f168628131572ea64fad4160b2742` (verified remote head).
Fresh packages and their exact source commit are recorded in
`preview-builds/gps-maintenance-20261112/` after the source commit is created.

## Audit: v2.0 versus v2.2.0

| Area | v2.0 | v2.2.0 / consequence |
| --- | --- | --- |
| Identity | Fixed RSA-2048 self-signed certificate, embedded public private key | Fresh P-256 self-signed identity on each server start; previous browser exceptions cannot carry over. Keep unique keys: restoring the shared private key would be unsafe. |
| Certificate | CN `3ds-gps-server`, CA=true, March 27, 2026 to March 24, 2036; no SAN or EKU | CN `3DS GPS local pairing`, no IP SAN or serverAuth EKU, fixed January 2026–2036 validity. Neither version has an automatically trusted identity. |
| Handshake | Repeated WANT_READ / WANT_WRITE retries | Single handshake call after introducing deadline-based nonblocking socket callbacks. Missing retries are a robustness regression, but callbacks already wait internally: this alone is NOT proof of the reported Safari failure. |
| Submission | No authentication; displayed success before delivery; malformed response lengths | Independent random 128-bit bearer token, JSON validation, correct HTTP response sizes, delivered count only after acknowledged success. Preserve these improvements. |
| URL / QR | Local HTTPS URL, no secret | Private token in fragment, removed by history.replaceState. QR host tests decode complete token-bearing URLs. Refresh then lost the token before this follow-up fix. |
| Lifetime | Blocking client operations | 10-second handshake / 5-second request deadline, serial client processing, bounded stop. A speculative idle browser connection can delay later clients; actual iOS timing still needs measurement. |

The observed Safari certificate error is consistent with certificate identity,
format and trust changes. Source inspection cannot establish which precise trust
evaluation failed on the user's phone. The old fixed certificate was itself
self-signed, lacked an IP SAN and used CA=true; it was not a generally trusted
Safari solution either. The reported v2.0 success is not disproved by this audit.

## Changes preserved and added

The starting branch's handshake retries, actual IPv4 SAN, serverAuth EKU,
digitalSignature usage, CA=false and RTC-derived validity are preserved.
Token entropy remains separate from public certificate serial entropy.

This follow-up retains the bearer token in same-origin, tab-scoped sessionStorage
so a page refresh still pairs. It validates missing/malformed pairing before
requesting location, diagnoses an insecure browser context, and renders browser
errors as text. If storage is disabled, the scanned page still works but reload
requires rescanning. A new server session always requires its new QR token.
Certificate serial allocation and own-certificate configuration failures are now
checked; an unspecified 0.0.0.0 identity is rejected. The header no longer
promises Safari can accept a warning once.

## Evidence and limits

| Check | Result |
| --- | --- |
| Full host gate | 32 suite results passed, no skips or failed suites; see `host-tests.txt`. Includes maps/cache/offline regions, panorama parsers, StreetGuess, globe, terrain/buildings, controls and UI logic. These are host checks, not console feature demonstrations. |
| Required source checks | `static-tls.txt`, `static-http.txt`: passed. |
| Required pairing / QR tests | Passed with `ASAN_OPTIONS=detect_leaks=0`; AddressSanitizer and UBSan remain enabled. OpenCV independently decoded six URLs and camera-style transformations. |
| Initial environment failures | LeakSanitizer cannot run under this environment's tracing; reran with only leak detection disabled. Missing OpenCV and host Jansson metadata were supplied, then the full suite passed without skips. |
| Embedded browser JS | `phone-js.txt`: actual extracted JS executed in Node with mocked browser APIs; refresh, missing token, insecure context, HTTP rejection, acknowledgment and throttling passed. Not a browser geolocation test. |
| Live certificate and TLS | `live-tls.txt`: compiled actual `source/gps_server.c` with POSIX service shims, native mbedTLS 2.28.10 and host Jansson. OpenSSL validated actual generated certificate IP match, wrong-IP rejection, current validity, P-256, SHA-256, leaf KU and serverAuth EKU. TLS 1.2 and HTTP passed after explicitly trusting that test certificate. Default public trust correctly FAILED for self-signing. Console links mbedTLS 2.28.8; host TLS does not verify console entropy, threading or sockets. |
| Security and lifecycle | Missing/wrong/duplicate token rejected; malformed/duplicate/out-of-range JSON rejected; fragmented requests accepted; stale token rejected after restart; no token in certificate/page/logs; stop interrupts an idle TLS client. |
| Console packages | Clean devkitARM build, package validation and hashes recorded alongside test binaries. GNU-stack linker warning remains from toolchain runtime. |
| Safari / physical 3DS | NOT TESTED. No physical iPhone/3DS or emulator executable available in this workspace. No claim of complete pairing or runtime feature verification. |

## Why the one-scan requirement remains open

Correct X.509 extensions do not establish trust in a self-signed issuer.
Apple documents that self-signed certificates cannot be verified without user
interaction. Geolocation requires a secure browser context. A publicly trusted
CA cannot issue a certificate directly for reserved private LAN IP addresses.

References checked November 12:

- https://support.apple.com/guide/deployment/depb5eff8914/web
- https://www.w3.org/TR/geolocation/#request-a-position
- https://cabforum.org/working-groups/server/baseline-requirements/requirements/

A promising architecture is a project-operated, publicly trusted HTTPS pairing
page plus an authenticated relay: both phone and 3DS make outbound HTTPS
connections, avoiding phone-to-local-certificate trust and mixed-content issues.
It needs a hosted endpoint, expiry/revocation, rate limits, location privacy and
preferably end-to-end encrypted fixes. No such endpoint exists in this repository
and none was deployed. This is an investigated next design, NOT a working or
Safari-verified substitute. A hosted page fetching the current self-signed local
endpoint would still encounter trust problems. Plain LAN HTTP was not substituted.

## Follow-up: iPhone hotspot connection lost after trusting (November 12)

The user installed and tested the maintenance candidate from source commit
`858f59e1dec89b99d9e7d8eb8589fd467a293919`. Safari now offered a
certificate warning with a continuation option, which the original v2.2.0
did not. However, after accepting it, Safari displayed
"The network connection was lost" at `172.20.10.6:8443` on a phone hotspot.
The browser GPS page was not loaded. This is a real hardware result,
**not** evidence of a successful fix.

A subsequent, source-only follow-up on this branch:

- Extends the **TLS handshake** window from 10 s to 60 s, to allow time for
  the user to review a browser certificate warning.
- Extends the **post-handshake HTTP** window from 5 s to 20 s.
- Retries recoverable `MBEDTLS_ERR_SSL_WANT_READ` / `WANT_WRITE` results in
  HTTP header/body reads and HTTP response writes.
- Adds stage-only log entries to distinguish accepted TCP, completed handshake,
  request read failure, timeout, and completed GPS page delivery. No pairing
  secret, coordinates, or HTTP request body should be logged.
- Adds long-pause scenarios to the native-host TLS regression script:
  12 seconds before beginning TLS, 7 seconds after handshake before sending GET,
  and 7 seconds during a partial location POST body. These are host simulations
  of delayed activity, **not** a substitute for real iPhone tests.

**The uploaded CIA and 3DSX in `preview-builds/gps-maintenance-20261112/`
were built from the earlier `858f59e1` source and DO NOT include this
follow-up. They must be rebuilt from the latest branch head before the next
physical test.** Preserve the old test binaries as provenance; publish
separate verified development artifacts with fresh hashes.

Possible remaining risk: this server processes one TLS client at a time, so
an abandoned connection can delay a replacement Safari connection until its
deadline. If the next hardware attempt still fails, inspect the new stage logs
and then investigate parallel connection handling rather than increasing
timeouts indefinitely.

Correcting the certificate and waiting for a manual exception does not
establish automatic Safari trust. **Do not announce one-scan GPS as working
or replace public release assets until the complete browser-geolocation path
has been verified on physical hardware.**

## Personal acceptance before publication

1. On the iPhone, test both Camera QR and full Safari from a fresh session. Record
   iOS version and whether the certificate screen prevents opening the page.
   If certificate installation or complicated exceptions are required, the
   intended one-scan requirement has failed; do not publish as fixed.
2. If the page opens, allow location, confirm `Fix #... delivered` and a moving
   3DS position. Check stationary accuracy, refresh, foreground/background return,
   stop/restart with a new QR, and repeated reconnects. Keep pairing QR private.
3. Confirm map exploration, live Street View/navigation/sharpening, StreetGuess,
   globe, terrain/buildings, offline packs and settings on physical hardware.

## Publication only after explicit approval

Keep tag/release `v2.2.0`; preserve v2.0. Replace existing assets with original
filenames `3ds_google_maps.cia` / `3ds_google_maps.3dsx` only after acceptance.
Update accurate hashes and source records at that time. Current release assets,
README links and existing QR are deliberately unchanged during testing.

The existing FBI QR decodes to:
`https://raw.githubusercontent.com/Oldhimaster1/3ds-Google-Maps/9cc4858b5fca0cb8d3806ead7ed0b960c7f144fb/preview-builds/rc3/3ds_google_maps.cia`.
That immutable QR will NEVER follow replacement release assets.

`fbi-v2.2.0-pending.png` is independently decoded and ready for later use. It
targets the stable CIA release URL. Until asset replacement, scanning it installs
the CURRENT published build, not this candidate. Minimum eventual change: use
that new QR image in README, update `[cia]` and `[three]` to the existing v2.2.0
release URLs, update `[sums]` to the new accurate record, and replace the stale
RC3 download label. No lengthy certificate explanation belongs in README.
