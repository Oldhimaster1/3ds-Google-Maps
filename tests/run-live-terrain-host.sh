#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/host/terrain_runtime_stubs -Iinclude \
  source/terrain_runtime.c source/terrain_math.c \
  tests/host/terrain_runtime_test.c -lm -o /tmp/3ds_live_terrain_test
ASAN_OPTIONS=detect_leaks=1 /tmp/3ds_live_terrain_test
rm -f /tmp/3ds_live_terrain_test
