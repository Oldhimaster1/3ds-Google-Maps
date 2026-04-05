#!/usr/bin/env python3
"""Find untagged emojis and their server context."""
import re, sys

path = r"c:\Users\mslag\3D Objects\(3) Discord _ @MysticMalard.mhtml"
with open(path, "r", encoding="utf-8", errors="ignore") as f:
    content = f.read()
decoded = content.replace("=\n", "").replace("=\r\n", "")

# All emoji IDs in file
all_ids = set(m.group(1) for m in re.finditer(r'cdn\.discordapp\.com/emojis/(\d{15,})', decoded))

# IDs tagged with "from SERVER"
tagged = {}
for m in re.finditer(
    r'aria-label=3D":([^:]+): from ([^"]+)"[^>]*src=3D"https://cdn\.discordapp\.com/emojis/(\d{15,})',
    decoded
):
    tagged[m.group(3)] = (m.group(1), m.group(2))

untagged = all_ids - set(tagged.keys())
print(f"Total emoji IDs: {len(all_ids)}")
print(f"Tagged (from server): {len(tagged)}")
print(f"Untagged: {len(untagged)}")

# Get names for untagged
for eid in sorted(untagged):
    for m in re.finditer(r'(.{400})cdn\.discordapp\.com/emojis/' + eid, decoded):
        ctx = m.group(1)
        name_m = re.search(r'alt=3D":(\w+):"', ctx[-200:])
        name = name_m.group(1) if name_m else "?"
        print(f"  {eid}: {name}")
        break

# Also look for server sidebar to identify which servers the user is in
print("\n\nServers in sidebar:")
for m in re.finditer(r'data-dnd-name=3D"(?!Above )([^"]+)"', decoded):
    name = m.group(1)
    if name not in ("undefined",):
        print(f"  {name}")
