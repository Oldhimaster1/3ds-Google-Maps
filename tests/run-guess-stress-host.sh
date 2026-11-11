#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude source/guess_core.c tests/host/guess_sampler_stress.c -lm -o /tmp/sg_stress_test
/tmp/sg_stress_test
rm -f /tmp/sg_stress_test