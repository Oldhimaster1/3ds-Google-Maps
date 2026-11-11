#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -Iinclude source/guess_challenge.c source/guess_locations.c tests/host/guess_challenge_test.c -o "$work/challenge"
"$work/challenge"
