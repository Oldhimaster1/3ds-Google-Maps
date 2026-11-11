#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude \
    source/street_recent.c tests/host/street_recent_test.c \
    -o /tmp/street_recent_test_3ds
/tmp/street_recent_test_3ds
rm -f /tmp/street_recent_test_3ds
echo "Street recently-visited cache tests passed"
