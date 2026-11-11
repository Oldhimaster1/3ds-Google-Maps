#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -O2 -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude source/pano_motion.c tests/host/pano_motion_test.c -lm -o /tmp/3ds_pano_motion_test
/tmp/3ds_pano_motion_test
rm -f /tmp/3ds_pano_motion_test