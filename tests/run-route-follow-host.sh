#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude source/route_follow.c tests/host/route_follow_test.c -lm -o /tmp/route_follow_host
/tmp/route_follow_host
rm /tmp/route_follow_host

${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude source/route_follow.c tests/host/route_follow_stress.c -lm -o /tmp/route_follow_stress
/tmp/route_follow_stress
rm /tmp/route_follow_stress
