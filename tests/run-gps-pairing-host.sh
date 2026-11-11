#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -g -O1 -fsanitize=address,undefined -Iinclude source/gps_pairing.c tests/host/gps_pairing_test.c -o "$work/pairing"
"$work/pairing"
