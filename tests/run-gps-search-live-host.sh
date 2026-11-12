#!/bin/sh
# Run host regression gates for GPS QR, geocoding UI helpers and map tile priority.
set -eu
cd "$(dirname "$0")/.."
echo "1/8 QR BCH and independent decoded top-screen QR"
sh tests/run-gps-qr-host.sh
echo "2/8 HTTPS GPS response framing and acknowledgement ordering"
python3 tests/host/gps_http_static_test.py
python3 tests/host/gps_tls_static_test.py
echo "3/8 Search input normalization"
sh tests/run-search-input-host.sh
echo "4/8 GPS follow jitter filtering"
sh tests/run-gps-follow-host.sh
echo "5/8 Live viewport tile relevance"
sh tests/run-live-tile-host.sh
echo "6/8 Existing map/cache/region tests"
sh tests/run-host.sh
echo "7/8 Existing panorama and input tests"
sh tests/run-multisource-host.sh
echo "8/8 StreetGuess regression tests"
sh tests/run-guess-all-host.sh
echo "Completed host regression gates. Native build, Wi-Fi and physical GPS scanning are separate."
