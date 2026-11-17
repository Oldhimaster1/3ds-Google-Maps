#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
 -Itests/host/text_stubs -Iinclude source/ui_text.c source/intl.c tests/host/ui_text_test.c -lfribidi -lm -o "$work/ui-text"
"$work/ui-text"
