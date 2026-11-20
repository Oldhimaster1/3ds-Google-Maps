#!/usr/bin/env python3
"""Validate glyph coverage in the shipped BCFNT, without a desktop font API."""
import json
from pathlib import Path
import re
import struct
root = Path(__file__).resolve().parents[2]
font = (root / 'data/unicode_font.bin').read_bytes()
assert font[:4] == b'CFNT'
assert struct.unpack_from('<I', font, 12)[0] == len(font)
glyphs = set()
# FINF's CMAP pointer and each next pointer address the payload,
# eight bytes after the block tag. Texture data is outside TGLP's header.
offset = struct.unpack_from('<I', font, 44)[0] - 8
visited = set()
while offset:
    assert offset not in visited
    visited.add(offset)
    tag, size = struct.unpack_from('<4sI', font, offset)
    assert tag == b'CMAP'
    assert size >= 8 and offset + size <= len(font)
    if tag == b'CMAP':
        first, last, method = struct.unpack_from('<HHH', font, offset + 8)
        if method == 0:
            glyphs.update(range(first, last + 1))
        elif method == 1:
            indices = struct.unpack_from('<' + 'H' * (last - first + 1), font, offset + 20)
            glyphs.update(first + i for i, glyph in enumerate(indices) if glyph != 0xffff)
        elif method == 2:
            count = struct.unpack_from('<H', font, offset + 20)[0]
            for i in range(count):
                cp, glyph = struct.unpack_from('<HH', font, offset + 22 + i * 4)
                if glyph != 0xffff:
                    glyphs.add(cp)
        else:
            raise AssertionError(method)
    next_payload = struct.unpack_from('<I', font, offset + 16)[0]
    offset = next_payload - 8 if next_payload else 0
source = (root / 'source/intl.c').read_text()
rows = re.findall(r'^    \{\{(.*)\}\},$', source, flags=re.M)
assert len(rows) > 100
for row in rows:
    translations = json.loads('[' + row + ']')
    assert len(translations) == 9
    placeholders = re.findall(r'%(?:\d+\$)?[\d.]*[sudi]', translations[0])
    for translation in translations:
        assert re.findall(r'%(?:\d+\$)?[\d.]*[sudi]', translation) == placeholders
        assert all(ord(ch) in glyphs for ch in translation), translation
for text in ['ﻡﻼﺳ', 'ﻉﺭﺎﺷ', 'Москва', 'İstanbul', 'Αθήνα', 'שלום', 'Tiếng Việt']:
    assert all(ord(ch) in glyphs for ch in text), text
print(f'PASS shipped BCFNT: {len(glyphs)} codepoints, {len(rows)} translated messages, Arabic presentation glyphs and format placeholders')
