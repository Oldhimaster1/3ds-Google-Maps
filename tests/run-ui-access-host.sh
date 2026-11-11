#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Iinclude source/ui_accessibility.c tests/host/ui_accessibility_test.c -o /tmp/3ds_ui_access_test
/tmp/3ds_ui_access_test
rm -f /tmp/3ds_ui_access_test