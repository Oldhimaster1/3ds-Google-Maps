#!/usr/bin/env python3
"""Validate nine-language catalog and generate the firmware's lookup table."""
import json
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
langs = ['en', 'ar', 'fr', 'es', 'de', 'pt', 'it', 'ru', 'tr']
pattern = re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%]')
messages = json.loads((root / 'assets/translations/messages.json').read_text())
rows = []
for english, translations in messages.items():
    assert set(translations) == set(langs), english
    expected = pattern.findall(english)
    for lang in langs:
        assert translations[lang] and pattern.findall(translations[lang]) == expected, (english, lang)
    assert translations['en'] == english
    rows.append('    {{' + ','.join(json.dumps(translations[lang], ensure_ascii=False) for lang in langs) + '}},')
(root / 'source/intl_messages.inc').write_text('\n'.join(rows) + '\n')
print(f'Validated and generated {len(messages)} messages in nine languages')
