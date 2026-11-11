#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -O1 -g -Itests/host/stubs -Iinclude \
  source/guess_core.c source/guess_roads.c source/ui_accessibility.c source/pano_motion.c source/guess_replay.c source/earth_options.c source/street_heading.c tests/host/guess_ui_smoke_test.c -lm -o /tmp/streetguess_ui_test
/tmp/streetguess_ui_test
rm -f /tmp/streetguess_ui_test
