#!/bin/sh
# Runs StreetGuess unit, randomized sampling and simulated 3DS UI suites.
set -eu
cd "$(dirname "$0")/.."
sh tests/run-guess-core-host.sh
sh tests/run-guess-stress-host.sh
sh tests/run-guess-ui-host.sh
