#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude source/guess_core.c tests/host/guess_core_test.c -lm -o /tmp/streetguess_core_test
/tmp/streetguess_core_test
rm -f /tmp/streetguess_core_test