#!/usr/bin/env python3
"""
download_region.py — Bulk tile downloader for 3DS Google Maps offline use.

Run this on your PC to download map tiles for a geographic region, then copy
ONE file to your 3DS SD card. The 3DS app reads tiles directly from the pack
file — no need to transfer thousands of individual files.

Tile sources:
  - satellite (default): Esri World Imagery — bulk download permitted
  - street: Esri World Street Map — bulk download permitted

NOTE: OpenStreetMap tile servers (tile.openstreetmap.org) prohibit bulk
downloading per their tile usage policy. This script does NOT use OSM servers.

Output:
    3ds_google_maps/tiles/sat.tilepack       (satellite)
    3ds_google_maps/tiles/street.tilepack    (street)

    Copy this file to sdmc:/3ds_google_maps/tiles/ on your SD card.
    One big file transfers MUCH faster than thousands of tiny PNG files.

    If a .tilepack already exists for the same source, new tiles are merged in.

Usage:
    python download_region.py --lat 40.7128 --lon -74.006 --radius-km 5 --zoom 10-15
    python download_region.py --bbox 32.03,-83.36,35.22,-78.54 --zoom 6-13 -w 32 -y
    python download_region.py --lat 40.7128 --lon -74.006 --radius-km 5 --street
"""

import argparse
import hashlib
import math
import os
import struct
import sys
import time
import threading
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

# Tile server URLs — sources that permit bulk downloads
ESRI_SAT_URL = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
ESRI_STREET_URL = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}"

USER_AGENT = "3DS-Google-Maps-Offline-Downloader/1.0"

# Tilepack binary format constants (must match include/tilepack.h)
TILEPACK_MAGIC = b"3DTP"
TILEPACK_VERSION = 1
TILEPACK_HEADER_SIZE = 16
TILEPACK_ENTRY_SIZE = 20

# Defaults
DELAY_SECONDS = 0.05   # 50ms between requests per worker
NUM_WORKERS = 8         # concurrent download threads


def lat_lon_to_tile(lat, lon, zoom):
    """Convert lat/lon to tile x,y at given zoom (Web Mercator)."""
    n = 1 << zoom
    x = int((lon + 180.0) / 360.0 * n)
    lat_rad = math.radians(lat)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    x = max(0, min(x, n - 1))
    y = max(0, min(y, n - 1))
    return x, y


def tile_to_lat_lon(tx, ty, zoom):
    """Convert tile x,y to lat/lon of the tile's NW corner."""
    n = 1 << zoom
    lon = tx / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * ty / n))))
    return lat, lon


def get_bbox_from_center(lat, lon, radius_km):
    """Get bounding box from center + radius in km."""
    # Rough degrees per km
    km_per_deg_lat = 111.0
    km_per_deg_lon = 111.0 * math.cos(math.radians(lat))
    if km_per_deg_lon < 1:
        km_per_deg_lon = 1

    dlat = radius_km / km_per_deg_lat
    dlon = radius_km / km_per_deg_lon

    return (lat - dlat, lon - dlon, lat + dlat, lon + dlon)


def count_tiles(bbox, min_zoom, max_zoom):
    """Count total tiles in a bbox across zoom range."""
    lat_min, lon_min, lat_max, lon_max = bbox
    total = 0
    for z in range(min_zoom, max_zoom + 1):
        tx_min, ty_min = lat_lon_to_tile(lat_max, lon_min, z)
        tx_max, ty_max = lat_lon_to_tile(lat_min, lon_max, z)
        total += (tx_max - tx_min + 1) * (ty_max - ty_min + 1)
    return total


def download_tiles(bbox, min_zoom, max_zoom, output_dir, satellite=False,
                   delay=DELAY_SECONDS, workers=NUM_WORKERS, auto_yes=False):
    """Download all tiles in bbox, pack into a single .tilepack file."""
    lat_min, lon_min, lat_max, lon_max = bbox
    source_dir = "sat" if satellite else "street"
    source_name = "Esri Satellite" if satellite else "Esri Street Map"

    total = count_tiles(bbox, min_zoom, max_zoom)
    print(f"\nRegion: lat [{lat_min:.4f}, {lat_max:.4f}]  lon [{lon_min:.4f}, {lon_max:.4f}]")
    print(f"Zoom:   {min_zoom} to {max_zoom}")
    print(f"Source: {source_name}")
    print(f"Tiles:  {total}")
    print(f"Workers: {workers}   Delay: {delay}s/request/worker")

    # Determine output pack path
    pack_dir = os.path.join(output_dir, "3ds_google_maps", "tiles")
    pack_path = os.path.join(pack_dir, f"{source_dir}.tilepack")
    print(f"Output: {pack_path}")

    # Load existing tilepack to merge with (if present)
    existing = load_tilepack(pack_path)
    if existing:
        print(f"  Existing pack has {len(existing)} tiles — will merge")

    # Build tile list, skipping tiles already in the pack
    tiles = []
    skipped = 0
    for z in range(min_zoom, max_zoom + 1):
        tx_min, ty_min = lat_lon_to_tile(lat_max, lon_min, z)
        tx_max, ty_max = lat_lon_to_tile(lat_min, lon_max, z)
        for tx in range(tx_min, tx_max + 1):
            for ty in range(ty_min, ty_max + 1):
                if (z, tx, ty) in existing:
                    skipped += 1
                else:
                    tiles.append((z, tx, ty))

    to_download = len(tiles)
    print(f"  To download: {to_download}  (already in pack: {skipped})")
    print()

    if to_download == 0:
        print("All tiles already in pack. Nothing to download.")
        return

    if not auto_yes:
        if to_download > 10000:
            print(f"WARNING: {to_download} tiles is a very large download.")
            resp = input("Continue? [y/N] ").strip().lower()
            if resp != "y":
                print("Cancelled.")
                return
        else:
            resp = input(f"Download {to_download} tiles? [Y/n] ").strip().lower()
            if resp == "n":
                print("Cancelled.")
                return

    # Shared state for concurrent downloads
    results = {}   # (z, x, y) -> bytes
    lock = threading.Lock()
    stats = {"done": 0, "downloaded": 0, "failed": 0}
    start_time = time.time()

    def download_one(z, tx, ty):
        if satellite:
            url = ESRI_SAT_URL.format(z=z, x=tx, y=ty)
        else:
            url = ESRI_STREET_URL.format(z=z, x=tx, y=ty)

        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            response = urllib.request.urlopen(req, timeout=30)
            data = response.read()
            if data:
                with lock:
                    results[(z, tx, ty)] = data
                    stats["downloaded"] += 1
            else:
                with lock:
                    stats["failed"] += 1
        except Exception:
            with lock:
                stats["failed"] += 1

        with lock:
            stats["done"] += 1

        if delay > 0:
            time.sleep(delay)

    # Progress printer
    stop_progress = threading.Event()

    def print_progress():
        while not stop_progress.is_set():
            with lock:
                d = stats["done"]
                dl = stats["downloaded"]
                fl = stats["failed"]
            elapsed = time.time() - start_time
            rate = d / elapsed if elapsed > 0 else 0
            eta = int((to_download - d) / rate) if rate > 0 else 0
            bar_w = 36
            filled = int(d * bar_w / to_download) if to_download > 0 else 0
            bar = "#" * filled + "-" * (bar_w - filled)
            print(
                f"\r  [{bar}] {d}/{to_download}  "
                f"new:{dl} fail:{fl}  "
                f"{rate:.1f} t/s  ETA:{eta}s   ",
                end="", flush=True
            )
            if d >= to_download:
                break
            stop_progress.wait(0.5)

    progress_thread = threading.Thread(target=print_progress, daemon=True)
    progress_thread.start()

    # Download with thread pool
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(download_one, z, tx, ty) for z, tx, ty in tiles]
        for f in as_completed(futures):
            pass

    stop_progress.set()
    progress_thread.join(timeout=2)

    dl_elapsed = time.time() - start_time
    print(f"\n\nDownloaded {stats['downloaded']} tiles in {int(dl_elapsed)}s")
    if stats["failed"]:
        print(f"  Failed: {stats['failed']}")

    # Merge with existing tiles and write the tilepack
    merged = {**existing, **results}
    write_tilepack(pack_path, merged)

    pack_size = os.path.getsize(pack_path)
    print(f"\nWrote {pack_path}")
    print(f"  {len(merged)} tiles, {pack_size / 1024 / 1024:.1f} MB")
    print()
    print(f"Copy to your SD card:  sdmc:/3ds_google_maps/tiles/{source_dir}.tilepack")


def load_tilepack(path):
    """Read an existing .tilepack file into a dict of {(z,x,y): bytes}.
    Returns empty dict if file doesn't exist or is invalid."""
    tiles = {}
    if not os.path.exists(path):
        return tiles

    try:
        with open(path, "rb") as f:
            hdr = f.read(TILEPACK_HEADER_SIZE)
            if len(hdr) < TILEPACK_HEADER_SIZE or hdr[:4] != TILEPACK_MAGIC:
                return tiles

            version, tile_count, data_offset = struct.unpack_from("<III", hdr, 4)
            if version != TILEPACK_VERSION:
                return tiles

            # Read index
            index_bytes = f.read(tile_count * TILEPACK_ENTRY_SIZE)
            if len(index_bytes) < tile_count * TILEPACK_ENTRY_SIZE:
                return tiles

            # Read each tile's data
            entries = []
            for i in range(tile_count):
                off = i * TILEPACK_ENTRY_SIZE
                zoom, _, _, _, x, y, t_offset, t_size = struct.unpack_from(
                    "<BBBBIIII", index_bytes, off
                )
                entries.append((zoom, x, y, t_offset, t_size))

            for zoom, x, y, t_offset, t_size in entries:
                f.seek(data_offset + t_offset)
                data = f.read(t_size)
                if len(data) == t_size:
                    tiles[(zoom, x, y)] = data

    except (OSError, struct.error):
        return {}

    return tiles


def write_tilepack(path, tiles_dict):
    """Write a dict of {(z,x,y): bytes} as a .tilepack binary file.
    
    Deduplicates identical tiles — tiles with the same content (e.g. ocean,
    empty land) are stored once in the data section, with multiple index
    entries pointing to the same offset. This typically saves 20-50% for
    large geographic regions.
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)

    # Sort keys for binary search on the 3DS
    sorted_keys = sorted(tiles_dict.keys())
    tile_count = len(sorted_keys)
    data_offset = TILEPACK_HEADER_SIZE + tile_count * TILEPACK_ENTRY_SIZE

    # Deduplicate: hash each tile, store unique tiles only once
    # Multiple index entries can point to the same (offset, size) in the data section
    hash_to_loc = {}   # sha256 -> (data_offset, size)
    unique_count = 0
    saved_bytes = 0

    index_buf = bytearray()
    data_buf = bytearray()
    offset = 0

    for z, x, y in sorted_keys:
        tile_data = tiles_dict[(z, x, y)]
        tile_hash = hashlib.sha256(tile_data).digest()

        if tile_hash in hash_to_loc:
            # Duplicate — reuse existing data location
            dup_offset, dup_size = hash_to_loc[tile_hash]
            entry = struct.pack("<BBBBIIII", z, 0, 0, 0, x, y, dup_offset, dup_size)
            saved_bytes += len(tile_data)
        else:
            # New unique tile
            hash_to_loc[tile_hash] = (offset, len(tile_data))
            entry = struct.pack("<BBBBIIII", z, 0, 0, 0, x, y, offset, len(tile_data))
            data_buf.extend(tile_data)
            offset += len(tile_data)
            unique_count += 1

        index_buf.extend(entry)

    dup_count = tile_count - unique_count
    if dup_count > 0:
        pct = saved_bytes * 100 / (saved_bytes + len(data_buf)) if (saved_bytes + len(data_buf)) > 0 else 0
        print(f"  Dedup: {unique_count} unique tiles, {dup_count} duplicates removed")
        print(f"  Saved: {saved_bytes / 1024 / 1024:.1f} MB ({pct:.0f}% reduction)")

    # Write file
    with open(path, "wb") as f:
        # Header: magic(4) + version(4) + tile_count(4) + data_offset(4)
        f.write(TILEPACK_MAGIC)
        f.write(struct.pack("<III", TILEPACK_VERSION, tile_count, data_offset))
        f.write(index_buf)
        f.write(data_buf)


def parse_zoom(zoom_str):
    """Parse zoom string like '10-15' or '12'."""
    if "-" in zoom_str:
        parts = zoom_str.split("-", 1)
        return int(parts[0]), int(parts[1])
    z = int(zoom_str)
    return z, z


def main():
    parser = argparse.ArgumentParser(
        description="Download map tiles for 3DS Google Maps offline use.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --lat 40.7128 --lon -74.006 --radius-km 5 --zoom 10-15
  %(prog)s --lat 48.8566 --lon 2.3522 --radius-km 3 --zoom 12-16 --street
  %(prog)s --bbox 32.03,-83.36,35.22,-78.54 --zoom 6-13 -w 32 -y
  %(prog)s --lat 35.6762 --lon 139.6503 --radius-km 10 --zoom 8-14 -o ~/Desktop/sd_tiles

Uses Esri tile servers which permit bulk downloads.
Does NOT use OpenStreetMap tile servers (their policy prohibits bulk downloads).

Output is a single .tilepack file — MUCH faster to transfer than thousands of
individual tile files. Copy it to sdmc:/3ds_google_maps/tiles/ on your SD card.
        """
    )

    # Location (either center+radius or bbox)
    loc = parser.add_argument_group("Location (pick one)")
    loc.add_argument("--lat", type=float, help="Center latitude")
    loc.add_argument("--lon", type=float, help="Center longitude")
    loc.add_argument("--radius-km", type=float, default=5,
                     help="Radius in km around center (default: 5)")
    loc.add_argument("--bbox", type=str,
                     help="Bounding box: lat_min,lon_min,lat_max,lon_max")

    # Zoom
    parser.add_argument("--zoom", "-z", type=str, default="10-14",
                        help="Zoom range, e.g. '10-15' or '12' (default: 10-14)")

    # Source (default: satellite)
    source_group = parser.add_mutually_exclusive_group()
    source_group.add_argument("--satellite", "-s", action="store_true", default=True,
                              help="Download Esri satellite tiles (default)")
    source_group.add_argument("--street", action="store_true",
                              help="Download Esri street map tiles instead of satellite")

    # Output
    parser.add_argument("-o", "--output", type=str, default=".",
                        help="Output directory (default: current directory)")

    # Rate limit
    parser.add_argument("--delay", type=float, default=DELAY_SECONDS,
                        help=f"Delay between requests per worker in seconds (default: {DELAY_SECONDS})")

    # Workers
    parser.add_argument("--workers", "-w", type=int, default=NUM_WORKERS,
                        help=f"Number of concurrent download threads (default: {NUM_WORKERS})")

    # Skip confirmation
    parser.add_argument("--yes", "-y", action="store_true",
                        help="Skip confirmation prompt")

    args = parser.parse_args()

    # Determine bounding box
    if args.bbox:
        parts = [float(x.strip()) for x in args.bbox.split(",")]
        if len(parts) != 4:
            parser.error("--bbox must be lat_min,lon_min,lat_max,lon_max")
        bbox = tuple(parts)
    elif args.lat is not None and args.lon is not None:
        bbox = get_bbox_from_center(args.lat, args.lon, args.radius_km)
    else:
        parser.error("Specify either --lat/--lon or --bbox")

    min_zoom, max_zoom = parse_zoom(args.zoom)
    if min_zoom < 0 or max_zoom > 19 or min_zoom > max_zoom:
        parser.error("Zoom must be 0-19 and min <= max")

    total = count_tiles(bbox, min_zoom, max_zoom)
    if total == 0:
        print("No tiles in the specified region.")
        return

    use_satellite = not args.street
    download_tiles(bbox, min_zoom, max_zoom, args.output,
                   satellite=use_satellite, delay=args.delay,
                   workers=args.workers, auto_yes=args.yes)


if __name__ == "__main__":
    main()
