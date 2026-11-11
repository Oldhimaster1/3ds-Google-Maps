#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
 -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
 source/detail_mesh.c tests/host/detail_mesh_test.c -lm -o "$work/detail-mesh"
ASAN_OPTIONS=detect_leaks=1 "$work/detail-mesh"
