#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fsanitize=undefined -Iinclude \
    tests/host/street_image_test.c source/street_image.c -o "$tmp/street-image-test"
"$tmp/street-image-test"
