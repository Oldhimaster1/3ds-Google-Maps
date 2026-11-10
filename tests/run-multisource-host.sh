#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
   -Iinclude tests/host/pano_gyro_test.c source/pano_gyro.c \
   -lm -o "$tmp/gyro"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
   -Iinclude tests/host/street_look_input_test.c source/street_look_input.c \
   -lm -o "$tmp/stick"
"$tmp/stick"
"$tmp/gyro"
if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists jansson; then
  echo "SKIP provider-parser native tests (install jansson development headers)"
  exit 0
fi
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
   -Iinclude $(pkg-config --cflags jansson) \
   tests/host/street_sources_test.c source/street_sources.c source/panorama_live.c \
   -lm $(pkg-config --libs jansson) -o "$tmp/providers"
"$tmp/providers"
