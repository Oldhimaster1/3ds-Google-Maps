#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
 -fsanitize=address,undefined -fno-omit-frame-pointer \
 -Itests/host/detail_runtime_stubs -Iinclude \
 source/detail_runtime.c source/detail_plan.c source/pano_detail.c \
 source/globe_detail.c tests/host/detail_runtime_test.c -lm -o "$work/detail-runtime"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$work/detail-runtime"
