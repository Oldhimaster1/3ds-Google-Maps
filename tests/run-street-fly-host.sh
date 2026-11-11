#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude \
    source/street_fly_logic.c tests/host/street_fly_logic_test.c \
    -o /tmp/street_fly_logic_test_3ds
/tmp/street_fly_logic_test_3ds
rm -f /tmp/street_fly_logic_test_3ds
echo "Street fly transition host tests passed"
