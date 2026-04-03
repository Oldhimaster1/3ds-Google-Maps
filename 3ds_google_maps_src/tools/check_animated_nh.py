#!/usr/bin/env python3
"""Check which Nintendo Homebrew emojis have animated (GIF) versions and download them."""

import os
import re
import urllib.request
import time

BASE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "discord_emojis")

def get_all_emoji_ids():
    """Read emoji IDs from the index file."""
    emojis = {}  # id -> name
    idx_path = os.path.join(BASE_DIR, "emoji_index.txt")
    with open(idx_path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.strip().split(" | ")
            if len(parts) >= 3:
                eid, name = parts[0], parts[1]
                emojis[eid] = name
    return emojis


def check_animated(emoji_id):
    """Check if an emoji has an animated GIF version."""
    url = f"https://cdn.discordapp.com/emojis/{emoji_id}.gif?size=96"
    try:
        req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=8) as resp:
            content_length = int(resp.headers.get("Content-Length", 0))
            return resp.status == 200 and content_length > 100
    except:
        return False


def download_gif(emoji_id, name):
    """Download the GIF version of an emoji."""
    url = f"https://cdn.discordapp.com/emojis/{emoji_id}.gif?size=96&quality=lossless"
    safe_name = re.sub(r'[~]', '_', name)
    filepath = os.path.join(BASE_DIR, f"{safe_name}_{emoji_id}.gif")
    
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = resp.read()
        with open(filepath, "wb") as f:
            f.write(data)
    return filepath, len(data)


if __name__ == "__main__":
    print("=== Checking for Animated Nintendo Homebrew Emojis ===\n")
    
    emojis = get_all_emoji_ids()
    print(f"Checking {len(emojis)} emojis for animated GIF versions...\n")
    
    animated_found = []
    
    for i, (eid, name) in enumerate(sorted(emojis.items(), key=lambda x: x[1].lower()), 1):
        print(f"  [{i}/{len(emojis)}] {name}...", end=" ", flush=True)
        if check_animated(eid):
            print("ANIMATED!")
            animated_found.append((eid, name))
        else:
            print("static")
        time.sleep(0.15)
    
    print(f"\n{'='*50}")
    print(f"Found {len(animated_found)} animated emojis out of {len(emojis)} total!\n")
    
    if animated_found:
        print("Animated emojis:")
        for eid, name in animated_found:
            print(f"  :{name}:")
        
        print(f"\nDownloading GIF versions...")
        for eid, name in animated_found:
            try:
                filepath, size = download_gif(eid, name)
                print(f"  :{name}: -> {os.path.basename(filepath)} ({size:,} bytes)")
                time.sleep(0.2)
            except Exception as e:
                print(f"  :{name}: FAILED: {e}")
        
        print("\nDone! GIF files saved alongside the static .webp versions.")
    else:
        print("No animated emojis found.")
