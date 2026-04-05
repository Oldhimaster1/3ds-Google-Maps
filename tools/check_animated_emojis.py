#!/usr/bin/env python3
"""Check which Ruby emojis have animated (GIF) versions and download them."""

import os
import re
import urllib.request
import time

BASE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "discord_emojis_ruby")

def get_all_emoji_ids():
    """Read emoji IDs from the index files."""
    emojis = {}  # id -> (name, folder)
    for folder_name in ["ruby_expressions", "ruby_plushies", "ruby_special"]:
        idx_path = os.path.join(BASE_DIR, folder_name, "emoji_index.txt")
        if not os.path.exists(idx_path):
            continue
        with open(idx_path) as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                parts = line.strip().split(" | ")
                if len(parts) >= 3:
                    eid, name = parts[0], parts[1]
                    emojis[eid] = (name, folder_name)
    return emojis


def check_animated(emoji_id):
    """Check if an emoji has an animated GIF version by doing a HEAD request."""
    url = f"https://cdn.discordapp.com/emojis/{emoji_id}.gif?size=96"
    try:
        req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=8) as resp:
            # If we get 200, the GIF exists
            content_type = resp.headers.get("Content-Type", "")
            content_length = int(resp.headers.get("Content-Length", 0))
            return resp.status == 200 and content_length > 100
    except:
        return False


def download_gif(emoji_id, name, folder):
    """Download the GIF version of an emoji."""
    url = f"https://cdn.discordapp.com/emojis/{emoji_id}.gif?size=96&quality=lossless"
    safe_name = re.sub(r'[~]', '_', name)
    filepath = os.path.join(BASE_DIR, folder, f"{safe_name}_{emoji_id}.gif")
    
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = resp.read()
        with open(filepath, "wb") as f:
            f.write(data)
    return filepath, len(data)


if __name__ == "__main__":
    print("=== Checking for Animated Ruby Emojis ===\n")
    
    emojis = get_all_emoji_ids()
    print(f"Checking {len(emojis)} emojis for animated GIF versions...\n")
    
    animated_found = []
    
    for i, (eid, (name, folder)) in enumerate(sorted(emojis.items(), key=lambda x: x[1][0].lower()), 1):
        print(f"  [{i}/{len(emojis)}] {name}...", end=" ", flush=True)
        if check_animated(eid):
            print("ANIMATED!")
            animated_found.append((eid, name, folder))
        else:
            print("static")
        time.sleep(0.15)  # Rate limit
    
    print(f"\n{'='*50}")
    print(f"Found {len(animated_found)} animated emojis out of {len(emojis)} total!\n")
    
    if animated_found:
        print("Animated emojis:")
        for eid, name, folder in animated_found:
            print(f"  :{name}: [{folder}]")
        
        print(f"\nDownloading GIF versions...")
        for eid, name, folder in animated_found:
            try:
                filepath, size = download_gif(eid, name, folder)
                print(f"  :{name}: -> {os.path.basename(filepath)} ({size:,} bytes)")
                time.sleep(0.2)
            except Exception as e:
                print(f"  :{name}: FAILED: {e}")
        
        print("\nDone! GIF files saved alongside the static .webp versions.")
    else:
        print("No animated emojis found.")
