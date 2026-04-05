#!/usr/bin/env python3
"""Extract and download Ruby's Paradise Discord emojis into 3 categorized folders."""

import re
import os
import urllib.request
import time

MHTML_PATH = r"c:\Users\mslag\3D Objects\(3) Discord _ @MysticMalard.mhtml"
BASE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "discord_emojis_ruby")

# 3 folders
FOLDER_EXPRESSIONS = os.path.join(BASE_DIR, "ruby_expressions")     # Emotion/reaction faces
FOLDER_PLUSHIES = os.path.join(BASE_DIR, "ruby_plushies")           # Plushies and figurines
FOLDER_SPECIAL = os.path.join(BASE_DIR, "ruby_special")             # Special, collabs, kana, alts

# Plushie/figurine keywords
PLUSHIE_NAMES = {
    "RubyPlushie", "RubyPlushie2", "SmolRubyPlushie", "SmolRubyPlushie2",
    "RubyIdolPlushie", "RubyIdolPlushie2", "RubyKittyPlushie",
    "RubySegaPlushie", "WoobyKawaiiPlushie", "RubyFatVer3", "RubyFat_Pat",
}

# Special/collab/alternate keywords
SPECIAL_NAMES = {
    "Ruby", "Ruby~1", "Ruby~2", "Ruby~3",
    "Ruby_bonk_tokyunn", "ruby_deathstare_tokyunn",
    "Ruby_stare_up", "Rubycute1", "Rubycute2",
    "RubyNapping", "RubyRave", "RubySobbing", "Ruby_Pat", "Ruby_Pat~1",
    "ruby_kana_1", "ruby_kana_2", "ruby_kana_3", "ruby_kana_4",
    "lying_ruby", "eepy_wooby",
}


def categorize(name):
    if name in PLUSHIE_NAMES:
        return FOLDER_PLUSHIES
    elif name in SPECIAL_NAMES:
        return FOLDER_SPECIAL
    else:
        return FOLDER_EXPRESSIONS


def extract_emojis(mhtml_path):
    with open(mhtml_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    decoded = content.replace("=\n", "").replace("=\r\n", "")

    emojis = {}  # id -> name

    # "from Ruby's Paradise" tagged emojis
    for m in re.finditer(
        r'aria-label=3D":([^:]+): from ([^"]+)"[^>]*src=3D"https://cdn\.discordapp\.com/emojis/(\d{15,})',
        decoded
    ):
        name, server, eid = m.group(1), m.group(2), m.group(3)
        if "Ruby" in server or "ruby" in server:
            emojis[eid] = name

    # Also alt-tagged emojis
    for m in re.finditer(
        r'alt=3D":(\w+):"[^>]*src=3D"https://cdn\.discordapp\.com/emojis/(\d{15,})',
        decoded
    ):
        name, eid = m.group(1), m.group(2)
        if eid not in emojis and ("ruby" in name.lower() or "wooby" in name.lower()):
            emojis[eid] = name

    # Check for animated
    animated = set()
    for m in re.finditer(r'cdn\.discordapp\.com/emojis/(\d{15,})\.gif', decoded):
        animated.add(m.group(1))
    for m in re.finditer(r'cdn\.discordapp\.com/emojis/(\d{15,})\.\w+\?[^"\'>\s]*animated', decoded):
        animated.add(m.group(1))

    return emojis, animated


def download_emojis(emojis, animated):
    # Create folders
    for folder in [FOLDER_EXPRESSIONS, FOLDER_PLUSHIES, FOLDER_SPECIAL]:
        os.makedirs(folder, exist_ok=True)

    # Categorize and count
    cats = {FOLDER_EXPRESSIONS: [], FOLDER_PLUSHIES: [], FOLDER_SPECIAL: []}
    for eid, name in sorted(emojis.items(), key=lambda x: x[1].lower()):
        folder = categorize(name)
        cats[folder].append((eid, name))

    print(f"\n  Expressions: {len(cats[FOLDER_EXPRESSIONS])} emojis")
    print(f"  Plushies:    {len(cats[FOLDER_PLUSHIES])} emojis")
    print(f"  Special:     {len(cats[FOLDER_SPECIAL])} emojis")

    total = len(emojis)
    count = 0
    success = 0
    failed = 0

    for folder, items in cats.items():
        folder_name = os.path.basename(folder)
        # Write index for this folder
        with open(os.path.join(folder, "emoji_index.txt"), "w") as idx:
            idx.write(f"# {folder_name} - Emoji Index\n")
            idx.write(f"# {len(items)} emojis\n\n")

            for eid, name in items:
                count += 1
                is_animated = eid in animated
                ext = "gif" if is_animated else "webp"
                # Sanitize name for filename
                safe_name = re.sub(r'[~]', '_', name)
                filename = f"{safe_name}_{eid}.{ext}"
                filepath = os.path.join(folder, filename)
                url = f"https://cdn.discordapp.com/emojis/{eid}.{ext}?size=96&quality=lossless"

                idx.write(f"{eid} | {name} | {'animated' if is_animated else 'static'} | {filename}\n")

                if os.path.exists(filepath):
                    print(f"  [{count}/{total}] [SKIP] {name}")
                    success += 1
                    continue

                try:
                    print(f"  [{count}/{total}] [{folder_name}] {name} ({'animated' if is_animated else 'static'})...")
                    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
                    with urllib.request.urlopen(req, timeout=10) as resp:
                        data = resp.read()
                        with open(filepath, "wb") as f:
                            f.write(data)
                    success += 1
                    time.sleep(0.2)
                except Exception as e:
                    print(f"         [FAIL] {e}")
                    # Try png fallback
                    try:
                        alt_url = f"https://cdn.discordapp.com/emojis/{eid}.png?size=96"
                        req = urllib.request.Request(alt_url, headers={"User-Agent": "Mozilla/5.0"})
                        with urllib.request.urlopen(req, timeout=10) as resp:
                            data = resp.read()
                            alt_file = f"{safe_name}_{eid}.png"
                            with open(os.path.join(folder, alt_file), "wb") as f:
                                f.write(data)
                        print(f"         -> Got as .png")
                        success += 1
                    except:
                        failed += 1
                    time.sleep(0.3)

    print(f"\nDone! {success} downloaded, {failed} failed out of {total} total")


if __name__ == "__main__":
    print("=== Ruby's Paradise Emoji Extractor ===\n")

    emojis, animated = extract_emojis(MHTML_PATH)

    print(f"Found {len(emojis)} Ruby emojis")
    print(f"Animated: {len(animated)}")

    # Print categorized list
    for eid, name in sorted(emojis.items(), key=lambda x: x[1].lower()):
        folder = os.path.basename(categorize(name))
        anim = " (animated)" if eid in animated else ""
        print(f"  [{folder}] :{name}:{anim}  ->  {eid}")

    print(f"\nDownloading to: {BASE_DIR}")
    download_emojis(emojis, animated)
