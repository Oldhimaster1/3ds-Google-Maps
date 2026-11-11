#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
for part in geo_formats gps_motion street_heading terrain_math building_mesh earth_options pano_detail globe_detail guess_replay; do
  echo "Testing $part"
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
    source/"$part".c tests/host/"$part"_test.c -lm -o /tmp/earth3d_test_"$part"
  ASAN_OPTIONS=detect_leaks=1 /tmp/earth3d_test_"$part"
  rm -f /tmp/earth3d_test_"$part"
done
