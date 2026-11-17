#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
 -Itests/host/audio_stubs -Iinclude source/ambient_music.c tests/host/ambient_music_test.c -lm -o "$work/audio-test"
"$work/audio-test"
