#!/usr/bin/env python3
"""Rebuild the bundled DejaVu font with devkitPro's mkbcfnt on PATH."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parent.parent
ranges = [(0x20, 0x24f), (0x370, 0x52f), (0x590, 0x6ff),
          (0x750, 0x77f), (0x8a0, 0x8ff), (0x1e00, 0x1eff),
          (0x2000, 0x206f), (0x20a0, 0x20cf),
          (0xfb1d, 0xfdff), (0xfe70, 0xfeff)]
with tempfile.NamedTemporaryFile(mode='w') as whitelist:
    whitelist.write(' '.join(f'0x{cp:04X}' for start, end in ranges
                             for cp in range(start, end + 1)))
    whitelist.flush()
    subprocess.run(['mkbcfnt', '-s', '24', '-w', whitelist.name,
                    '-o', str(root / 'data/unicode_font.bin'),
                    str(root / 'assets/fonts/DejaVuSans.ttf')], check=True)
