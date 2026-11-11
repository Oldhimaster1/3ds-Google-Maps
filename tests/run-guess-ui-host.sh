#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Itests/host/stubs -Iinclude \
  source/guess_challenge.c source/guess_locations.c source/guess_core.c source/guess_roads.c source/ui_accessibility.c source/pano_motion.c source/guess_replay.c source/earth_options.c source/street_heading.c tests/host/guess_ui_smoke_test.c -lm -o "$work/streetguess-ui"
(cd "$work" && ./streetguess-ui)
