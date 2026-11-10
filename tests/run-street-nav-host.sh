#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fsanitize=undefined -Iinclude \
    tests/host/street_nav_test.c source/street_nav.c -lm -o "$tmp/street-nav-test"
"$tmp/street-nav-test"
