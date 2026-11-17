"""Exercise every catalog format through the real C formatter in all locales."""
import ctypes
import json
from pathlib import Path
import re
import subprocess
import tempfile
from font_coverage_test import glyphs
root = Path(__file__).resolve().parents[2]
messages = json.loads((root / 'assets/translations/messages.json').read_text())
langs = ['en', 'ar', 'fr', 'es', 'de', 'pt', 'it', 'ru', 'tr']
pattern = re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%]')
with tempfile.TemporaryDirectory() as work:
    library = Path(work) / 'intl.so'
    subprocess.run(['cc', '-std=c11', '-shared', '-fPIC', '-I' + str(root / 'include'),
                    str(root / 'source/intl.c'), '-lfribidi', '-o', str(library)], check=True)
    api = ctypes.CDLL(str(library))
    api.intl_set_language.argtypes = [ctypes.c_uint]
    api.intl_visual.argtypes = [ctypes.c_char_p,ctypes.c_char_p,ctypes.c_size_t]
    api.intl_visual.restype = ctypes.c_bool
    visual = ctypes.create_string_buffer(4096)
    shaped = 0
    api.intl_snprintf.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    libc = ctypes.CDLL(None)
    libc.snprintf.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    tested = 0
    for english, translations in messages.items():
        for lang in langs:
            assert api.intl_visual(translations[lang].encode(),visual,len(visual)), (english,lang)
            assert all(ord(ch) in glyphs or ch.isspace() for ch in visual.value.decode()), (english,lang,visual.value.decode())
            shaped += 1
        fields = pattern.findall(english)
        if not fields:
            continue
        args = []
        for field in fields:
            if field == '%%':
                continue
            args.extend(ctypes.c_int(5) for _ in range(field.count('*')))
            kind = field[-1]
            if kind == 's':
                args.append(ctypes.c_char_p(b'Example'))
            elif kind in 'fFeEgGaA':
                args.append(ctypes.c_double(12.5))
            elif kind == 'c':
                args.append(ctypes.c_int(65))
            elif 'll' in field:
                args.append(ctypes.c_ulonglong(7))
            elif 'l' in field:
                args.append(ctypes.c_ulong(7))
            elif 'z' in field:
                args.append(ctypes.c_size_t(7))
            else:
                args.append(ctypes.c_uint(7))
        expected = ctypes.create_string_buffer(4096)
        actual = ctypes.create_string_buffer(4096)
        for index, lang in enumerate(langs):
            api.intl_set_language(index)
            libc.snprintf(expected, len(expected), translations[lang].encode(), *args)
            api.intl_snprintf(actual, len(actual), english.encode(), *args)
            assert actual.value == expected.value, (english, lang, actual.value.decode(), expected.value.decode())
            tested += 1
    print(f'PASS {shaped} shaped translations with shipped glyph coverage')
    print(f'PASS {tested} translated format cases: real C varargs, numeric fields, precision, percent signs and nine locales')
