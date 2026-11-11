#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined \
 -fno-omit-frame-pointer -Iinclude source/qrcode.c tests/host/qr_encoder_test.c -o /tmp/3ds_gps_qr_test
/tmp/3ds_gps_qr_test
rm /tmp/3ds_gps_qr_test
python3 tests/host/qr_decode_test.py
