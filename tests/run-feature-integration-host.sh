#!/bin/sh
# Source-only release gate. Compile and run locally with GCC/Clang; additionally
# use devkitARM and on-device tests before claiming a working CIA.
set -eu
cd "$(dirname "$0")/.."
python3 tests/host/feature_wiring_test.py
sh tests/run-input-remap-host.sh
sh tests/run-detail-plan-host.sh
sh tests/run-detail-mesh-host.sh
sh tests/run-detail-runtime-host.sh
sh tests/run-earth3d-host.sh
sh tests/run-route-follow-host.sh
sh tests/run-guess-all-host.sh
echo 'Feature integration host checks: PASS'
