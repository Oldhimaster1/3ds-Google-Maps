#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fsanitize=undefined \
    -Itests/host -Iinclude tests/host/region_test.c source/region.c source/tilepack.c \
    -lm -o "$work/region-test"
"$work/region-test"
