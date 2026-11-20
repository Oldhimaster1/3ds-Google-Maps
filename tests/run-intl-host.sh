#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
 -Iinclude $(pkg-config --cflags fribidi) source/intl.c tests/host/intl_test.c \
 $(pkg-config --libs fribidi) -o "$work/intl-test"
(cd "$work" && ./intl-test)
python3 tests/host/font_coverage_test.py
