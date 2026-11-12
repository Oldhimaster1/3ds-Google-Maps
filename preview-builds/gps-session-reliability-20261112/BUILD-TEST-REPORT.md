# GPS session reliability development build

- Source branch: `fix-gps-session-reliability-20261112`
- Source commit used for binaries: `c015006a515c339481b29fb2db3963b808b75e49`
- Build: devkitPro/devkitARM (`make -j2`), then `make cia` with makerom 0.18.4 and bannertool 1.2.3
- Packages: fresh CIA and 3DSX, compiled from the same source commit

## Checks

- `python3 tools/verify_build.py .`: passed; CIA and 3DSX headers valid and title ID is `000400000FD34B00`.
- `ASAN_OPTIONS=detect_leaks=0 bash tests/run-all-host.sh`: passed (all host suite scripts exited successfully). Leak detection was disabled because LeakSanitizer fails under this execution environment; AddressSanitizer and UBSan remain enabled. The optional Jansson-dependent native panorama/provider-parser checks were skipped because host Jansson development headers are unavailable.
- `python3 tests/host/gps_page_test.py`: passed browser-page logic checks; explicitly does not verify Safari, real location permissions, or phone networking.
- `python3 tests/host/gps_tls_live_test.py` with host mbedTLS 2.28.9 and Jansson headers: passed live POSIX TLS/HTTP tests, including certificate SAN/EKU/key usage, public trust rejection of the self-signed certificate, pairing authorization, malformed fixes, token rotation, delayed handshakes, slow body delivery, and server shutdown.
- `bash tests/run-gps-pairing-host.sh`: passed bearer-token authorization checks under ASan/UBSan.
- `python3 tests/host/gps_tls_static_test.py` and `python3 tests/host/gps_http_static_test.py`: passed after updating assertions that still described the superseded 60/20-second timeouts and fragment-only QR handoff.

- FBI QR: `fbi-install.png` independently decoded with OpenCV and exactly matches the commit-pinned URL in `FBI_CIA_URL` (raw GitHub URL pinned to binary commit `0d50f36e2dc104ecedaa953fe1838d2685aad8a3`).
- Recovery verification: reran the full `ASAN_OPTIONS=detect_leaks=0 bash tests/run-all-host.sh` suite successfully, reran the GPS page and live TLS tests successfully, confirmed the root build outputs byte-for-byte match the preview packages, and revalidated both recorded SHA-256 hashes.

## Not tested

- No emulator was available, so emulator install/launch is unverified.
- No physical 3DS or iPhone/Safari test was performed. The local self-signed certificate still requires manual browser trust handling; this build does not establish Safari trust or prove real GPS pairing.
