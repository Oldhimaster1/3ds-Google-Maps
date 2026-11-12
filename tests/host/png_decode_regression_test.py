#!/usr/bin/env python3
"""Host regression: accept safe 256x256 PNG tiles; reject malformed sizes/bytes.

Tests the actual source/simple_png.c with libpng and AddressSanitizer/UBSan.
Generates inputs using only Python's standard library.
"""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
RUNNER = r'''
#include "simple_png.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    assert(argc == 7);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size > 0 && size < 1024*1024);
    rewind(f);
    unsigned char *bytes = malloc((size_t)size);
    assert(bytes);
    assert(fread(bytes, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    int width = 0, height = 0;
    unsigned char *tiled = decode_png(bytes, (int)size, &width, &height);
    int valid = atoi(argv[2]);
    if (valid) {
        assert(tiled && width == 256 && height == 256);
        /* First Morton pixel, in GPU's ABGR byte order. */
        for (int i=0;i<4;i++) assert(tiled[i] == (unsigned char)atoi(argv[3+i]));
    } else {
        assert(!tiled);
    }
    free_png_data(tiled);
    free(bytes);
    return 0;
}
'''

def chunk(tag, body):
    return (struct.pack('>I', len(body)) + tag + body +
            struct.pack('>I', zlib.crc32(tag + body) & 0xffffffff))

def png(width, height, color_type, pixel, extras=b''):
    header = struct.pack('>IIBBBBB', width, height, 8, color_type, 0, 0, 0)
    scanlines = (b'\x00' + pixel * width) * height
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + extras +
            chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))

def run_one(exe, folder, name, data, expected=None):
    path = folder / (name + '.png')
    path.write_bytes(data)
    channels = expected if expected else (0, 0, 0, 0)
    subprocess.run([str(exe), str(path), str(int(expected is not None)),
                    *map(str, channels)], check=True, timeout=20)
    print('PASS', name)

with tempfile.TemporaryDirectory(prefix='3ds-png-test-') as td:
    folder = Path(td)
    (folder / 'runner.c').write_text(RUNNER)
    exe = folder / 'png-test'
    cc = os.environ.get('CC', 'cc')
    subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'include'),
                    str(ROOT / 'source/simple_png.c'),
                    str(folder / 'runner.c'), '-lpng', '-lz', '-o', str(exe)],
                   check=True, timeout=40)

    rgba = png(256, 256, 6, bytes([5, 6, 7, 8]))
    rgb = png(256, 256, 2, bytes([12, 34, 56]))
    pal_extra = chunk(b'PLTE', bytes([12, 34, 56])) + chunk(b'tRNS', bytes([180]))
    palette = png(256, 256, 3, bytes([0]), pal_extra)
    gray_alpha = png(256, 256, 4, bytes([51, 90]))
    run_one(exe, folder, 'RGBA_256', rgba, (8, 7, 6, 5))
    run_one(exe, folder, 'RGB_256', rgb, (255, 56, 34, 12))
    run_one(exe, folder, 'palette_transparency_256', palette, (180, 56, 34, 12))
    run_one(exe, folder, 'gray_alpha_256', gray_alpha, (90, 51, 51, 51))

    for w, h in [(8, 1), (1, 8), (9, 9), (256, 1), (257, 256),
                 (512, 512), (256, 257)]:
        run_one(exe, folder, f'invalid_dimensions_{w}x{h}',
                png(w, h, 6, bytes([5, 6, 7, 8])))
    run_one(exe, folder, 'truncated_pixel_data', rgba[:len(rgba)//2])
    run_one(exe, folder, 'invalid_signature', b'not-a-png')
    print('PASS 13 PNG decoder cases under AddressSanitizer/UBSan')
