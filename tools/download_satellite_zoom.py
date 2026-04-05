"""Download and stitch an entire Esri World Imagery zoom level.

Examples:
  python tools/download_satellite_zoom.py 1
  python tools/download_satellite_zoom.py 3 --output zoom3.png --workers 24
    python tools/download_satellite_zoom.py 2 --direct
    python tools/download_satellite_zoom.py 2 --proxy-base http://192.168.1.118:8080

Proxy mode is the default and fetches from this repo's /sat/<z>/<x>/<y>.png endpoint.
Direct mode fetches from Esri's World Imagery export endpoint.
"""

from __future__ import annotations

import argparse
import os
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Optional

import requests

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "Pillow is required. Install it with: pip install pillow requests"
    ) from exc


SAT_TILE_BASE = "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer"
DEFAULT_PROXY_BASE = os.environ.get("PROXY_BASE", "http://192.168.1.118:8080").strip()
WEB_MERCATOR_HALF_WORLD = 20037508.342789244
TILE_SIZE = 256
USER_AGENT = "3DS-Maps-Zoom-Downloader/1.0"
DEFAULT_RETRIES = 4


def format_bytes(num_bytes: int) -> str:
    units = ("B", "KiB", "MiB", "GiB")
    value = float(num_bytes)
    unit_index = 0
    while value >= 1024.0 and unit_index < len(units) - 1:
        value /= 1024.0
        unit_index += 1
    return f"{value:.2f} {units[unit_index]}"


def format_duration(seconds: float) -> str:
    total_seconds = max(0, int(round(seconds)))
    hours, remainder = divmod(total_seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours > 0:
        return f"{hours:02d}:{minutes:02d}:{secs:02d}"
    return f"{minutes:02d}:{secs:02d}"


def default_work_dir(output_path: Path) -> Path:
    return output_path.parent / f".{output_path.stem}_tiles"


def tile_file_path(work_dir: Path, z: int, x: int, y: int) -> Path:
    return work_dir / str(z) / str(x) / f"{y}.png"


def write_tile_file(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".part")
    temp_path.write_bytes(content)
    temp_path.replace(path)


def collect_existing_tiles(work_dir: Path, z: int, tiles_per_side: int) -> tuple[int, int]:
    existing = 0
    total_bytes = 0
    for y in range(tiles_per_side):
        for x in range(tiles_per_side):
            path = tile_file_path(work_dir, z, x, y)
            if path.is_file():
                existing += 1
                try:
                    total_bytes += path.stat().st_size
                except OSError:
                    pass
    return existing, total_bytes


def load_tile_image(work_dir: Path, z: int, x: int, y: int) -> tuple[int, int, Image.Image]:
    tile_path = tile_file_path(work_dir, z, x, y)
    with Image.open(tile_path) as tile_image:
        return x, y, tile_image.convert("RGB")


def stitch_tiles(
    work_dir: Path,
    z: int,
    tiles_per_side: int,
    image_width: int,
    image_height: int,
    stitch_workers: int,
) -> Image.Image:
    canvas = Image.new("RGB", (image_width, image_height))
    stitch_started = time.perf_counter()
    last_progress_update = stitch_started
    stitched = 0
    tile_count = tiles_per_side * tiles_per_side

    print(f"Stitching saved tiles into final image with {stitch_workers} workers...", flush=True)
    futures: dict[object, tuple[int, int]] = {}
    with ThreadPoolExecutor(max_workers=stitch_workers) as executor:
        for y in range(tiles_per_side):
            for x in range(tiles_per_side):
                future = executor.submit(load_tile_image, work_dir, z, x, y)
                futures[future] = (x, y)

        for future in as_completed(futures):
            x, y, tile_image = future.result()
            canvas.paste(tile_image, (x * TILE_SIZE, y * TILE_SIZE))

            stitched += 1
            now = time.perf_counter()
            if stitched == tile_count or (now - last_progress_update) >= 0.5:
                elapsed_s = now - stitch_started
                tiles_per_second = stitched / elapsed_s if elapsed_s > 0.0 else 0.0
                remaining = tile_count - stitched
                eta_s = remaining / tiles_per_second if tiles_per_second > 0.0 else 0.0
                progress_pct = (stitched / tile_count) * 100.0 if tile_count else 100.0
                print(
                    "Stitching: "
                    f"{stitched}/{tile_count} ({progress_pct:.1f}%) | "
                    f"elapsed {format_duration(elapsed_s)} | "
                    f"eta {format_duration(eta_s)} | "
                    f"rate {tiles_per_second:.2f} tiles/s",
                    flush=True,
                )
                last_progress_update = now

    return canvas


def build_satellite_url(z: int, x: int, y: int, proxy_base: Optional[str]) -> str:
    if proxy_base:
        return f"{proxy_base.rstrip('/')}/sat/{z}/{x}/{y}.png"

    tiles = float(1 << z)
    world = WEB_MERCATOR_HALF_WORLD * 2.0
    min_x = (x / tiles) * world - WEB_MERCATOR_HALF_WORLD
    max_x = ((x + 1) / tiles) * world - WEB_MERCATOR_HALF_WORLD
    max_y = WEB_MERCATOR_HALF_WORLD - (y / tiles) * world
    min_y = WEB_MERCATOR_HALF_WORLD - ((y + 1) / tiles) * world

    return (
        f"{SAT_TILE_BASE}/export?"
        f"bbox={min_x:.6f},{min_y:.6f},{max_x:.6f},{max_y:.6f}"
        f"&bboxSR=102100&imageSR=102100&size=256,256"
        f"&format=png32&transparent=false&f=image"
    )


def fetch_tile(z: int, x: int, y: int, proxy_base: Optional[str], timeout: float, retries: int) -> tuple[int, int, bytes, int, float, int]:
    url = build_satellite_url(z, x, y, proxy_base)
    last_error: Exception | None = None

    for attempt in range(retries + 1):
        started = time.perf_counter()
        try:
            response = requests.get(
                url,
                headers={"User-Agent": USER_AGENT, "Accept": "image/png,*/*"},
                timeout=timeout,
            )
            response.raise_for_status()
            elapsed_ms = (time.perf_counter() - started) * 1000.0
            return x, y, response.content, len(response.content), elapsed_ms, attempt
        except requests.RequestException as exc:
            last_error = exc
            if attempt >= retries:
                break
            time.sleep(min(2.0, 0.25 * (attempt + 1)))

    assert last_error is not None
    raise last_error


def default_output_path(zoom: int, proxy_base: Optional[str]) -> Path:
    suffix = "proxy" if proxy_base else "direct"
    return Path(f"satellite_zoom_{zoom}_{suffix}.png")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download every satellite tile at a zoom level and stitch them into one image."
    )
    parser.add_argument("zoom", type=int, help="Zoom level to download (recommended: 0-5).")
    parser.add_argument("--output", type=Path, default=None, help="Output PNG path.")
    parser.add_argument(
        "--workers",
        type=int,
        default=min(32, max(4, (os.cpu_count() or 8) * 2)),
        help="Parallel download workers.",
    )
    parser.add_argument(
        "--stitch-workers",
        type=int,
        default=max(4, (os.cpu_count() or 8)),
        help="Parallel tile decode workers used during the final stitch step.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=20.0,
        help="Per-request timeout in seconds.",
    )
    parser.add_argument(
        "--proxy-base",
        default=DEFAULT_PROXY_BASE,
        help="Proxy base URL. Defaults to the app proxy server.",
    )
    parser.add_argument(
        "--direct",
        action="store_true",
        help="Bypass the proxy and fetch satellite tiles directly from Esri.",
    )
    parser.add_argument(
        "--proxy-bypass",
        action="store_true",
        help="Alias for --direct.",
    )
    parser.add_argument(
        "--png-compress-level",
        type=int,
        default=1,
        help="PNG compression level 0-9. Lower is faster, higher is smaller.",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=DEFAULT_RETRIES,
        help="Retries per tile before it is marked as failed.",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=None,
        help="Directory for checkpointed downloaded tiles. Defaults next to the output file.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    zoom = args.zoom
    proxy_base = None if (args.direct or args.proxy_bypass) else args.proxy_base
    if zoom < 0 or zoom > 8:
        raise SystemExit("Zoom must be between 0 and 8 for practical desktop stitching.")
    if args.png_compress_level < 0 or args.png_compress_level > 9:
        raise SystemExit("--png-compress-level must be between 0 and 9.")
    if args.retries < 0:
        raise SystemExit("--retries must be 0 or greater.")
    if args.stitch_workers <= 0:
        raise SystemExit("--stitch-workers must be greater than 0.")

    tiles_per_side = 1 << zoom
    tile_count = tiles_per_side * tiles_per_side
    output_path = (args.output or default_output_path(zoom, proxy_base)).resolve()
    work_dir = (args.work_dir.resolve() if args.work_dir else default_work_dir(output_path).resolve())

    image_width = tiles_per_side * TILE_SIZE
    image_height = tiles_per_side * TILE_SIZE

    print(f"Downloading zoom {zoom}: {tile_count} tiles ({tiles_per_side}x{tiles_per_side})")
    print(f"Mode: {'proxy' if proxy_base else 'direct'}")
    if proxy_base:
        print(f"Proxy: {proxy_base}")
    print(f"Workers: {args.workers}")
    print(f"Stitch workers: {args.stitch_workers}")
    print(f"Output: {output_path}")
    print(f"Work dir: {work_dir}")
    print(f"PNG compression: {args.png_compress_level}")
    print(f"Retries per tile: {args.retries}")

    work_dir.mkdir(parents=True, exist_ok=True)
    resumed_tiles, resumed_bytes = collect_existing_tiles(work_dir, zoom, tiles_per_side)

    completed = resumed_tiles
    total_bytes = resumed_bytes
    total_request_ms = 0.0
    downloaded_tiles = 0
    retry_events = 0
    failed_tiles: list[tuple[int, int, str]] = []
    started = time.perf_counter()
    last_progress_update = started
    progress_interval_s = 0.5
    futures: dict[object, tuple[int, int]] = {}

    print(f"Resuming with {resumed_tiles}/{tile_count} tiles already on disk ({format_bytes(resumed_bytes)})")

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        for y in range(tiles_per_side):
            for x in range(tiles_per_side):
                tile_path = tile_file_path(work_dir, zoom, x, y)
                if tile_path.is_file():
                    continue
                future = executor.submit(fetch_tile, zoom, x, y, proxy_base, args.timeout, args.retries)
                futures[future] = (x, y)

        for future in as_completed(futures):
            submitted_x, submitted_y = futures[future]
            try:
                x, y, tile_content, tile_bytes, request_ms, attempts_used = future.result()
                tile_path = tile_file_path(work_dir, zoom, x, y)
                write_tile_file(tile_path, tile_content)
                completed += 1
                downloaded_tiles += 1
                total_bytes += tile_bytes
                total_request_ms += request_ms
                retry_events += attempts_used
            except (requests.RequestException, OSError) as exc:
                failed_tiles.append((submitted_x, submitted_y, str(exc)))

            now = time.perf_counter()
            if completed == tile_count or (now - last_progress_update) >= progress_interval_s:
                elapsed_s = now - started
                tiles_per_second = completed / elapsed_s if elapsed_s > 0.0 else 0.0
                average_request_ms = total_request_ms / downloaded_tiles if downloaded_tiles else 0.0
                remaining_tiles = tile_count - completed
                eta_s = remaining_tiles / tiles_per_second if tiles_per_second > 0.0 else 0.0
                progress_pct = (completed / tile_count) * 100.0 if tile_count else 100.0
                bytes_per_second = total_bytes / elapsed_s if elapsed_s > 0.0 else 0.0

                print(
                    "Progress: "
                    f"{completed}/{tile_count} ({progress_pct:.1f}%) | "
                    f"elapsed {format_duration(elapsed_s)} | "
                    f"eta {format_duration(eta_s)} | "
                    f"rate {tiles_per_second:.2f} tiles/s | "
                    f"data {format_bytes(total_bytes)} | "
                    f"throughput {format_bytes(int(bytes_per_second))}/s | "
                    f"avg req {average_request_ms:.1f} ms | "
                    f"resume {resumed_tiles} | "
                    f"new {downloaded_tiles} | "
                    f"retries {retry_events} | "
                    f"failed {len(failed_tiles)}",
                    flush=True,
                )
                last_progress_update = now

    if failed_tiles:
        print("Download stopped with failed tiles. Progress has been checkpointed and can be resumed.")
        print(f"Failed tiles: {len(failed_tiles)}")
        for x, y, message in failed_tiles[:10]:
            print(f"  z{zoom}/{x}/{y}: {message}")
        if len(failed_tiles) > 10:
            print(f"  ... and {len(failed_tiles) - 10} more")
        print(f"Re-run the same command to resume from {completed}/{tile_count} completed tiles.")
        return 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    canvas = stitch_tiles(work_dir, zoom, tiles_per_side, image_width, image_height, args.stitch_workers)
    canvas.save(output_path, compress_level=args.png_compress_level, optimize=False)
    total_elapsed_s = time.perf_counter() - started
    average_request_ms = total_request_ms / downloaded_tiles if downloaded_tiles else 0.0
    output_size = output_path.stat().st_size if output_path.exists() else 0
    print("Run complete")
    print(f"Tiles: {completed}/{tile_count}")
    print(f"Resumed tiles: {resumed_tiles}")
    print(f"Newly downloaded tiles: {downloaded_tiles}")
    print(f"Retry events: {retry_events}")
    print(f"Elapsed: {format_duration(total_elapsed_s)}")
    print(f"Download data: {format_bytes(total_bytes)}")
    print(f"Average tile request: {average_request_ms:.1f} ms")
    print(f"Average throughput: {format_bytes(int(total_bytes / total_elapsed_s))}/s" if total_elapsed_s > 0.0 else "Average throughput: n/a")
    print(f"Output image: {image_width}x{image_height}")
    print(f"Output file size: {format_bytes(output_size)}")
    print(f"Saved stitched image to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())