#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sh tests/run-ui-access-host.sh
sh tests/run-pano-motion-host.sh
sh tests/run-guess-ui-host.sh
echo "UI, gyro and simulated StreetGuess checks finished"
