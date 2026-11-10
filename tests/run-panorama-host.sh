#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists jansson; then
    echo "SKIP panorama native tests: install host jansson development package"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
    -Iinclude $(pkg-config --cflags jansson) \
    tests/host/panorama_live_test.c source/panorama_live.c \
    -lm $(pkg-config --libs jansson) -o "$work/pano-live-test"
"$work/pano-live-test"
