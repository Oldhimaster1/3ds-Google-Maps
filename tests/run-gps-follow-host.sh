#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined \
 -fno-omit-frame-pointer -Iinclude tests/host/gps_follow_gate_test.c -lm -o /tmp/3ds_gps_follow_test
/tmp/3ds_gps_follow_test
rm /tmp/3ds_gps_follow_test
