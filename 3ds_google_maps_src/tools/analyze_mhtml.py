#!/usr/bin/env python3
"""Analyze MHTML file to find server names and emoji groupings."""
import re

path = r"c:\Users\mslag\3D Objects\(3) Discord _ @MysticMalard.mhtml"
with open(path, "r", encoding="utf-8", errors="ignore") as f:
    content = f.read()

# Remove QP soft line breaks
decoded = content.replace("=\n", "").replace("=\r\n", "")

print(f"File size: {len(decoded):,} chars")

# Extract emojis with their server origin from aria-label "from SERVER_NAME"
# Pattern: :NAME: from SERVER  ... cdn.discordapp.com/emojis/ID
server_emojis = {}  # server -> {id: name}
all_emojis = {}     # id -> (name, server)

# Pattern: aria-label=3D":NAME: from SERVER" src=3D"...emojis/ID.ext"
for m in re.finditer(
    r'aria-label=3D":([^:]+): from ([^"]+)"[^>]*src=3D"https://cdn\.discordapp\.com/emojis/(\d{15,})',
    decoded
):
    name, server, eid = m.group(1), m.group(2), m.group(3)
    if server not in server_emojis:
        server_emojis[server] = {}
    server_emojis[server][eid] = name
    all_emojis[eid] = (name, server)

# Also non-QP encoded version
for m in re.finditer(
    r'aria-label=":([^:]+): from ([^"]+)"[^>]*src="https://cdn\.discordapp\.com/emojis/(\d{15,})',
    decoded
):
    name, server, eid = m.group(1), m.group(2), m.group(3)
    if server not in server_emojis:
        server_emojis[server] = {}
    server_emojis[server][eid] = name
    all_emojis[eid] = (name, server)

# Also get emojis with alt tag but without "from" (local server emojis)
for m in re.finditer(
    r'alt=3D":(\w+):"[^>]*src=3D"https://cdn\.discordapp\.com/emojis/(\d{15,})',
    decoded
):
    name, eid = m.group(1), m.group(2)
    if eid not in all_emojis:
        server_emojis.setdefault("_local", {})[eid] = name
        all_emojis[eid] = (name, "_local")

print(f"\nFound {len(all_emojis)} unique emojis across {len(server_emojis)} servers:\n")
for server in sorted(server_emojis.keys()):
    emojis = server_emojis[server]
    print(f"=== {server} ({len(emojis)} emojis) ===")
    for eid, name in sorted(emojis.items(), key=lambda x: x[1].lower()):
        print(f"  :{name}:  ->  {eid}")
    print()
