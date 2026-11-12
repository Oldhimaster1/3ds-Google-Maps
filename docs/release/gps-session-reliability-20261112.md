# GPS reliability investigation — 2026-11-12

## Scope and status

**Development branch only. Not approved for production.**
Starting ref: `fix-gps-https-browser-compat-20261112`, commit `8e6ef41`.
New branch: `fix-gps-session-reliability-20261112`.

The user supplied a 71-second iPhone screen recording of a real attempt on
November 12. It showed all three symptoms:

1. Safari rejects the untrusted local certificate at `172.20.10.6`.
2. Safari sometimes displays "the network connection was lost."
3. The 3DS GPS Bridge page can open but says
   "GPS send failed: 3DS returned HTTP 403."

The 403 is an *application authorization rejection*. In the production code
`handle_client()` returns 403 only for a `POST /location` whose bearer token
fails `gps_pairing_authorized()`. The video alone does NOT show whether the
pairing token was stale, lost on a Safari tab transition, or changed through
a deliberate/unintended server restart.

## Confirmed source defect

In `source/main.c`, tapping the Server source-mode button while the HTTPS
server was **already running** called `gps_cleanup()`, then `gps_init()`.
This regenerated the certificate and the per-session 128-bit token. An old
Safari tab kept the previous token, so the next location POST got HTTP 403.
This re-tap path is now idempotent; the separate start/stop card still gives
the user an explicit way to restart, which deliberately revokes prior pairing.

## Additional robustness findings

- A single server thread processes HTTPS clients serially. A stalled TLS
  handshake could monopolize it for 60 seconds, delaying real clients.
  The handshake allowance is now 16 seconds; the post-handshake HTTP
  allowance is 8 seconds. This reduces worst-case blocking, but a proper
  concurrent/nonblocking connection scheduler may still be needed if tests
  show that Safari routinely opens multiple connections.
- The old browser page required a fresh `watchPosition` event to retry
  a failed location POST. A stationary phone could remain disconnected
  indefinitely. The current page retains the latest fix, retries with
  bounded exponential backoff, and sends periodic acknowledgable updates.
- The new private QR embeds the current pairing token in a short HTTPS
  query parameter (`?pair=...`) instead of relying exclusively on the
  URL fragment. Safari certificate-warning navigation is expected to retain
  the query more reliably; the page clears it from browser history immediately
  after saving the token. The server sets Referrer-Policy: no-referrer and
  does not log the token. This is a plausible repair, not hardware confirmation.
- Legacy fragment links remain accepted for compatibility.
- A stale bearer token now stops automatic POSTs, clears same-tab storage,
  and visibly asks for the CURRENT private QR. It does not silently
  reuse a bad token or bypass authorization.
- The page suspends sending while backgrounded, resuming on foreground.
  It uses only textContent for errors.

## Validation performed

- Executed the actual updated embedded webpage JavaScript in an isolated
  V8 environment with mocked browser, storage, location, fetch, and timers:
  **12 checks passed**, plus a later QR-query handoff regression pass, including fresh scan, refresh, missing/insecure
  pairing, authenticated POST, throttling, retry without motion, HTTP 403
  invalidation, and foreground recovery.
- Copied the existing `gps_pairing.c` logic verbatim into a native host
  harness and compiled with AddressSanitizer and UndefinedBehaviorSanitizer.
  **5,000 valid** and **14,975 invalid** authorization checks passed across
  200 randomized session tokens.
- Extended the repository's *actual-server* POSIX/mbedTLS integration test
  with 12 extra valid and 12 extra rejected POSTs per session and an abandoned
  socket head-of-line test. **These added tests have NOT been executed** in
  this environment, because a compatible mbedTLS 2.28 host setup and
  devkitARM/3DS toolchain are not installed here.
- No real iPhone or Nintendo 3DS hardware could be controlled from this
  environment. The source changes are NOT yet a verified stable fix.

## Acceptance criteria BEFORE a release update

Run these on both the original 3DS and, if possible, the New 3DS using
an iPhone and the same Wi-Fi/hotspot used in the recording:

1. Scan CURRENT private QR and clear Safari's certificate interstitial.
   Check successful location permission, **20 consecutive acknowledged fixes**,
   and a visible map position.
2. Tap Server again while running: existing Safari tab must stay paired.
3. With the iPhone stationary, interrupt connectivity for 10 seconds and
   restore it: updates must recover without new movement or a restart.
4. Reload Safari and background/foreground Safari: updates resume.
5. Stop/restart server: old tab must say to scan the CURRENT QR; new scan
   must work, with no stale token accepted.
6. Try three freshly opened Camera sessions, three Safari sessions, and
   10 repeated start/stop cycles.
7. Keep the page foregrounded for at least 15 minutes and confirm that
   GPS fix count and location age continue updating on the console.
8. Repeat while normal map tiles download; inspect for network stalls.
9. Collect sanitized console stage logs (accepted, handshake complete,
   HTTP response, auth rejection, timeout) if any case fails.
10. Confirm non-GPS settings, offline tiles, Street View and StreetGuess
    remain unchanged; release only verified binaries with fresh hashes.

## Hard platform constraint

Safari's certificate warning is not resolved by changing X.509 SAN/EKU.
A new self-signed certificate cannot become universally trusted on stock iOS
through an ordinary QR code. A fully warning-free, one-scan web GPS setup
would require a trusted HTTPS origin and an appropriate relay architecture
(or some other trusted service/app/certificate arrangement). Do not
claim otherwise or weaken bearer-token validation, use insecure HTTP with
browser geolocation, or restore a globally shared private key.

## Release protection

Do **NOT** merge to main, replace public v2.2.0 assets, or publish this as a
stable GPS fix yet. Do not reuse the old verified CIA/3DSX because they were
built before the new changes. A new devkitPro build and hashes are required.
