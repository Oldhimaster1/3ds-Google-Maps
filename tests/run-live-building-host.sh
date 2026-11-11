#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
 -fsanitize=address,undefined -fno-omit-frame-pointer \
 -Itests/host/terrain_runtime_stubs -Iinclude \
 source/building_runtime.c source/building_mesh.c source/terrain_math.c \
 tests/host/building_runtime_test.c -lcurl -lm -o /tmp/3ds_building_runtime_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" /tmp/3ds_building_runtime_test
rm -f /tmp/3ds_building_runtime_test
