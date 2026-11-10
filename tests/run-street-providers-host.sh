#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=undefined \
    -Iinclude tests/host/gyro_stable_test.c source/gyro_stable.c -lm -o "$tmp/gyro"
"$tmp/gyro"
if ! command -v pkg-config >/dev/null || ! pkg-config --exists jansson; then
    echo "SKIP provider C tests: install the host jansson development headers"
    exit 0
fi
cc -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=undefined \
    -Iinclude $(pkg-config --cflags jansson) \
    tests/host/street_sources_test.c source/street_sources.c source/panorama_live.c \
    -lm $(pkg-config --libs jansson) -o "$tmp/providers"
"$tmp/providers"
