#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined \
 -fno-omit-frame-pointer -Iinclude tests/host/tile_priority_test.c -o /tmp/3ds_live_tile_test
/tmp/3ds_live_tile_test
rm /tmp/3ds_live_tile_test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined \
 -fno-omit-frame-pointer -Iinclude tests/host/tile_priority_stress.c -o /tmp/3ds_live_tile_stress
/tmp/3ds_live_tile_stress
rm -f /tmp/3ds_live_tile_stress
