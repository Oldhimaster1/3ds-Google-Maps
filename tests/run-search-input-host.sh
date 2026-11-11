#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined \
 -fno-omit-frame-pointer -Iinclude tests/host/search_input_test.c -o /tmp/3ds_search_test
/tmp/3ds_search_test
rm -f /tmp/3ds_search_test
