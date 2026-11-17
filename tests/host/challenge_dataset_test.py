"""Check the expanded manifest without requiring live provider access."""
import hashlib
import json
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
old = json.loads((root / 'docs/datasets/streetguess-v2.json').read_bytes())
raw = (root / 'docs/datasets/streetguess-v3.json').read_bytes()
new = json.loads(raw)
locations = new['locations']
assert new['version'] == 3 and 100 <= len(locations) <= 256
assert locations[:len(old['locations'])] == old['locations']
assert len({x['id'] for x in locations}) == len(locations)
assert len({x['country'] for x in locations}) >= 33
for x in locations:
    assert -90 <= x['lat'] <= 90 and -180 <= x['lon'] <= 180
    assert x['credit'] and x['license'] in {'cc-by-sa-4.0', 'cc-by-4.0', 'etalab-2.0'}
    assert x['metadata_url'].startswith('https://api.panoramax.xyz/api/collections/')
    assert x['metadata_url'].endswith('/items/' + x['id'])
    if x not in old['locations']:
        assert x['image_size'] == [2048, 1024]
    assert re.fullmatch('[0-9a-f]{64}', x['image_sha256'])
source = (root / 'source/guess_locations.c').read_text()
assert '0x' + hashlib.sha256(raw).hexdigest()[:8] + 'u' in source
print(f'PASS dataset v3: {len(locations)} unique panoramas, preserved v2 prefix, credits, image hashes and generated ID')
