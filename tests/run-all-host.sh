#!/bin/sh
# Full checkout regression gate. Set ASAN_OPTIONS=detect_leaks=0 only where
# LeakSanitizer is unavailable; AddressSanitizer and UBSan remain enabled.
set -u
cd "$(dirname "$0")/.."
failed=0
for suite in tests/run-*-host.sh tests/run-host.sh; do
 case "$suite" in *run-all-host.sh) continue;; esac
 echo "=== $suite ==="
 if sh "$suite"; then echo "PASS $suite"; else echo "FAIL $suite"; failed=1; fi
done
for suite in tests/explore_assets_test.py; do
 if python3 "$suite"; then echo "PASS $suite"; else echo "FAIL $suite"; failed=1; fi
done
exit "$failed"
