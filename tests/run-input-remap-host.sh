#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
 -fsanitize=address,undefined -fno-omit-frame-pointer \
 -Itests/host/input_remap_stubs -Iinclude \
 source/input_remap.c tests/host/input_remap_test.c -o "$work/remap"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$work/remap"
