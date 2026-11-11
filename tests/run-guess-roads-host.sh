#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude \
  source/guess_roads.c tests/host/guess_roads_test.c -lm -o /tmp/streetguess_roads_test
/tmp/streetguess_roads_test
rm -f /tmp/streetguess_roads_test
python3 tools/build_guess_roads.py --input tests/host/guess_roads_fixture.geojson usa \
   --output /tmp/streetguess_roads_generated_test.bin --seed 9 --max-records 1000 >/dev/null
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude \
  source/guess_roads.c tests/host/guess_roads_generated_test.c -lm -o /tmp/streetguess_roads_generated_test
/tmp/streetguess_roads_generated_test /tmp/streetguess_roads_generated_test.bin
rm -f /tmp/streetguess_roads_generated_test /tmp/streetguess_roads_generated_test.bin
