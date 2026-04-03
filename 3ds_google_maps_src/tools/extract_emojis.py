#!/usr/bin/env python3
"""Extract Discord custom emojis from an MHTML file and download them."""

import re
import os
import sys
import urllib.request
import time

MHTML_PATH = r"c:\Users\mslag\3D Objects\(1) Discord _ @MysticMalard.mhtml"
OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "discord_emojis")

def extract_emojis(mhtml_path):
    """Extract emoji IDs and names from MHTML file."""
    with open(mhtml_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    print(f"File size: {len(content):,} chars")

    emojis = {}  # id -> name

    # MHTML uses quoted-printable encoding: = is =3D, line wraps use =\n
    # Decode quoted-printable soft line breaks first
    decoded = content.replace("=\n", "").replace("=\r\n", "")
    
    # Pattern 1: alt=3D":name:" ... src=3D"https://cdn.discordapp.com/emojis/ID.ext"
    for m in re.finditer(
        r'alt=3D":(\w+):"[^>]*src=3D"https://cdn\.discordapp\.com/emojis/(\d{15,})\.\w+',
        decoded
    ):
        emojis[m.group(2)] = m.group(1)

    # Pattern 2: alt=":name:" ... src="https://cdn.discordapp.com/emojis/ID.ext" (non-encoded)
    for m in re.finditer(
        r'alt=":(\w+):"[^>]*src="https://cdn\.discordapp\.com/emojis/(\d{15,})\.\w+',
        decoded
    ):
        if m.group(2) not in emojis:
            emojis[m.group(2)] = m.group(1)

    # Pattern 3: aria-label with emoji name
    for m in re.finditer(
        r'aria-label=3D"[^"]*:(\w+):"[^>]*cdn\.discordapp\.com/emojis/(\d{15,})',
        decoded
    ):
        if m.group(2) not in emojis:
            emojis[m.group(2)] = m.group(1)

    # Pattern 4: data-name or title attributes
    for m in re.finditer(
        r'(?:data-name|title)=3D":(\w+):"[^>]*cdn\.discordapp\.com/emojis/(\d{15,})',
        decoded
    ):
        if m.group(2) not in emojis:
            emojis[m.group(2)] = m.group(1)

    # Also get ALL emoji IDs (even unnamed ones) from the file
    all_ids = set()
    for m in re.finditer(r'cdn\.discordapp\.com/emojis/(\d{15,})', decoded):
        all_ids.add(m.group(1))

    # Check for animated emojis (they typically use .gif)
    animated = set()
    for m in re.finditer(r'cdn\.discordapp\.com/emojis/(\d{15,})\.gif', decoded):
        animated.add(m.group(1))
    # Also check animated=true parameter
    for m in re.finditer(r'cdn\.discordapp\.com/emojis/(\d{15,})\.\w+\?[^"\'>\s]*animated=3Dtrue', decoded):
        animated.add(m.group(1))
    for m in re.finditer(r'cdn\.discordapp\.com/emojis/(\d{15,})\.\w+\?[^"\'>\s]*animated=true', decoded):
        animated.add(m.group(1))

    # For unnamed emojis, use their ID as name
    for eid in all_ids:
        if eid not in emojis:
            emojis[eid] = f"unknown_{eid}"

    return emojis, animated


def download_emojis(emojis, animated, output_dir):
    """Download all emojis to output directory."""
    os.makedirs(output_dir, exist_ok=True)

    print(f"\nDownloading {len(emojis)} emojis to: {output_dir}")
    
    # Create index file
    with open(os.path.join(output_dir, "emoji_index.txt"), "w") as idx:
        idx.write("# Discord Emoji Index\n")
        idx.write("# Format: ID | Name | Animated | Filename\n\n")
        
        success = 0
        failed = 0
        
        for eid, name in sorted(emojis.items(), key=lambda x: x[1].lower()):
            is_animated = eid in animated
            ext = "gif" if is_animated else "webp"
            filename = f"{name}_{eid}.{ext}"
            filepath = os.path.join(output_dir, filename)
            
            # Build URL - use size=96 for good quality
            url = f"https://cdn.discordapp.com/emojis/{eid}.{ext}?size=96&quality=lossless"
            
            idx.write(f"{eid} | {name} | {'animated' if is_animated else 'static'} | {filename}\n")
            
            if os.path.exists(filepath):
                print(f"  [SKIP] {name} (already exists)")
                success += 1
                continue
                
            try:
                print(f"  [{success+failed+1}/{len(emojis)}] Downloading {name} ({'animated' if is_animated else 'static'})...")
                req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
                with urllib.request.urlopen(req, timeout=10) as resp:
                    data = resp.read()
                    with open(filepath, "wb") as f:
                        f.write(data)
                success += 1
                time.sleep(0.2)  # Rate limit
            except Exception as e:
                print(f"  [FAIL] {name}: {e}")
                # Try alternate format
                alt_ext = "png" if ext == "webp" else "webp"
                alt_url = f"https://cdn.discordapp.com/emojis/{eid}.{alt_ext}?size=96"
                try:
                    req = urllib.request.Request(alt_url, headers={"User-Agent": "Mozilla/5.0"})
                    with urllib.request.urlopen(req, timeout=10) as resp:
                        data = resp.read()
                        alt_filename = f"{name}_{eid}.{alt_ext}"
                        with open(os.path.join(output_dir, alt_filename), "wb") as f:
                            f.write(data)
                    print(f"         -> Got it as .{alt_ext} instead")
                    success += 1
                except Exception as e2:
                    print(f"         -> Also failed as .{alt_ext}: {e2}")
                    failed += 1
                time.sleep(0.3)
    
    print(f"\nDone! {success} downloaded, {failed} failed out of {len(emojis)} total")


if __name__ == "__main__":
    print("=== Discord Emoji Extractor ===\n")
    
    if not os.path.exists(MHTML_PATH):
        print(f"MHTML file not found: {MHTML_PATH}")
        sys.exit(1)
    
    emojis, animated = extract_emojis(MHTML_PATH)
    
    # Print summary
    named = {k: v for k, v in emojis.items() if not v.startswith("unknown_")}
    unnamed = {k: v for k, v in emojis.items() if v.startswith("unknown_")}
    
    print(f"\nFound {len(emojis)} unique emojis:")
    print(f"  Named: {len(named)}")
    print(f"  Unnamed: {len(unnamed)}")
    print(f"  Animated: {len(animated)}")
    
    print("\n--- Named Emojis ---")
    for eid, name in sorted(named.items(), key=lambda x: x[1].lower()):
        anim_str = " (animated)" if eid in animated else ""
        print(f"  :{name}:{anim_str}  ->  {eid}")
    
    # Ask to download
    if "--download" in sys.argv or "-d" in sys.argv:
        download_emojis(emojis, animated, OUTPUT_DIR)
    else:
        print(f"\nRun with --download to download all emojis to {OUTPUT_DIR}")
