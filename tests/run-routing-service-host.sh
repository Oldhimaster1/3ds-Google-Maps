#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work="${TMPDIR:-/tmp}/routing-service-host"
rm -f "$work"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude source/routing_service.c tests/host/routing_service_test.c \
  -ljansson -lm -o "$work"
"$work"
rm -f "$work"
