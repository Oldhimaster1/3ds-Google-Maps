"""Interactive desktop satellite tile viewer.

Examples:
  python tools/satellite_viewer.py
  python tools/satellite_viewer.py --zoom 6 --lat 40.7128 --lon -74.0060
  python tools/satellite_viewer.py --direct

Controls:
  - Drag with left mouse button to pan
  - Mouse wheel to zoom in/out around the cursor
  - Double-click to zoom in
  - Right-click to recenter
  - +/- to zoom
  - 0 to reset view
"""

from __future__ import annotations

import argparse
import math
import os
import queue
import threading
from concurrent.futures import Future, ThreadPoolExecutor
from io import BytesIO
from pathlib import Path
from typing import Optional

import requests

try:
    import tkinter as tk
except ImportError as exc:
    raise SystemExit("Tkinter is required for the viewer.") from exc

try:
    from PIL import Image, ImageTk
except ImportError as exc:
    raise SystemExit("Pillow is required. Install it with: pip install pillow requests") from exc


SAT_TILE_BASE = "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer"
DEFAULT_PROXY_BASE = os.environ.get("PROXY_BASE", "http://192.168.1.118:8080").strip()
WEB_MERCATOR_HALF_WORLD = 20037508.342789244
TILE_SIZE = 256
MIN_ZOOM = 0
MAX_ZOOM = 18
USER_AGENT = "3DS-Maps-Satellite-Viewer/1.0"


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


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


def lon_to_world_x(lon: float, zoom: int) -> float:
    scale = TILE_SIZE * (1 << zoom)
    return ((lon + 180.0) / 360.0) * scale


def lat_to_world_y(lat: float, zoom: int) -> float:
    scale = TILE_SIZE * (1 << zoom)
    lat = clamp(lat, -85.05112878, 85.05112878)
    sin_lat = math.sin(math.radians(lat))
    mercator = 0.5 - math.log((1.0 + sin_lat) / (1.0 - sin_lat)) / (4.0 * math.pi)
    return mercator * scale


def world_x_to_lon(world_x: float, zoom: int) -> float:
    scale = TILE_SIZE * (1 << zoom)
    return (world_x / scale) * 360.0 - 180.0


def world_y_to_lat(world_y: float, zoom: int) -> float:
    scale = TILE_SIZE * (1 << zoom)
    mercator = math.pi * (1.0 - 2.0 * (world_y / scale))
    lat_rad = math.atan(math.sinh(mercator))
    return math.degrees(lat_rad)


def tile_cache_path(cache_dir: Path, z: int, x: int, y: int) -> Path:
    return cache_dir / str(z) / str(x) / f"{y}.png"


class TileFetcher:
    def __init__(self, cache_dir: Path, proxy_base: Optional[str], timeout: float, workers: int):
        self.cache_dir = cache_dir
        self.proxy_base = proxy_base
        self.timeout = timeout
        self.executor = ThreadPoolExecutor(max_workers=workers)
        self.completed: queue.Queue[tuple[int, int, int, Image.Image, bool] | tuple[int, int, int, None, bool]] = queue.Queue()
        self.pending: dict[tuple[int, int, int], Future] = {}
        self.pending_lock = threading.Lock()
        self.session = requests.Session()
        self.session.headers.update({"User-Agent": USER_AGENT, "Accept": "image/png,*/*"})

    def close(self) -> None:
        self.executor.shutdown(wait=False, cancel_futures=True)
        self.session.close()

    def request(self, z: int, x: int, y: int) -> None:
        key = (z, x, y)
        with self.pending_lock:
            if key in self.pending:
                return
            future = self.executor.submit(self._load_tile, z, x, y)
            self.pending[key] = future
            future.add_done_callback(lambda done, key=key: self._complete(key, done))

    def _complete(self, key: tuple[int, int, int], future: Future) -> None:
        with self.pending_lock:
            self.pending.pop(key, None)

        z, x, y = key
        try:
            image, from_cache = future.result()
            self.completed.put((z, x, y, image, from_cache))
        except (requests.RequestException, OSError):
            self.completed.put((z, x, y, None, False))

    def _load_tile(self, z: int, x: int, y: int) -> tuple[Image.Image, bool]:
        path = tile_cache_path(self.cache_dir, z, x, y)
        if path.is_file():
            with Image.open(path) as image:
                return image.convert("RGB"), True

        url = build_satellite_url(z, x, y, self.proxy_base)
        response = self.session.get(url, timeout=self.timeout)
        response.raise_for_status()

        path.parent.mkdir(parents=True, exist_ok=True)
        temp_path = path.with_suffix(path.suffix + ".part")
        temp_path.write_bytes(response.content)
        temp_path.replace(path)

        with Image.open(BytesIO(response.content)) as image:
            return image.convert("RGB"), False


class SatelliteViewer:
    def __init__(self, args: argparse.Namespace):
        self.zoom = int(clamp(args.zoom, MIN_ZOOM, MAX_ZOOM))
        self.center_lat = clamp(args.lat, -85.05112878, 85.05112878)
        self.center_lon = clamp(args.lon, -180.0, 180.0)
        self.proxy_base = None if (args.direct or args.proxy_bypass) else args.proxy_base
        self.cache_dir = (args.cache_dir.resolve() if args.cache_dir else Path(__file__).resolve().parent / ".satellite_viewer_cache")
        self.fetcher = TileFetcher(self.cache_dir, self.proxy_base, args.timeout, args.workers)
        self.root = tk.Tk()
        self.root.title("Satellite Viewer")
        self.root.geometry(args.window_size)
        self.canvas = tk.Canvas(self.root, bg="#0f1720", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.status_var = tk.StringVar()
        self.status = tk.Label(self.root, textvariable=self.status_var, anchor="w")
        self.status.pack(fill=tk.X)

        self.drag_start: tuple[int, int] | None = None
        self.drag_center_world: tuple[float, float] | None = None
        self.tk_tiles: dict[tuple[int, int, int], ImageTk.PhotoImage] = {}
        self.tile_sources: dict[tuple[int, int, int], str] = {}
        self.failed_tiles: set[tuple[int, int, int]] = set()
        self.stats_loaded = 0
        self.stats_cache_hits = 0
        self.stats_errors = 0

        self._bind_events()
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.render()
        self.root.after(50, self.poll_tiles)

    def _bind_events(self) -> None:
        self.canvas.bind("<Configure>", lambda _event: self.render())
        self.canvas.bind("<ButtonPress-1>", self.on_drag_start)
        self.canvas.bind("<B1-Motion>", self.on_drag_move)
        self.canvas.bind("<Double-Button-1>", lambda event: self.zoom_at(event.x, event.y, self.zoom + 1))
        self.canvas.bind("<Button-3>", self.on_recenter)
        self.canvas.bind("<MouseWheel>", self.on_mousewheel)
        self.canvas.bind("<Button-4>", lambda event: self.zoom_at(event.x, event.y, self.zoom + 1))
        self.canvas.bind("<Button-5>", lambda event: self.zoom_at(event.x, event.y, self.zoom - 1))
        self.root.bind("+", lambda _event: self.zoom_at(self.canvas.winfo_width() // 2, self.canvas.winfo_height() // 2, self.zoom + 1))
        self.root.bind("=", lambda _event: self.zoom_at(self.canvas.winfo_width() // 2, self.canvas.winfo_height() // 2, self.zoom + 1))
        self.root.bind("-", lambda _event: self.zoom_at(self.canvas.winfo_width() // 2, self.canvas.winfo_height() // 2, self.zoom - 1))
        self.root.bind("0", lambda _event: self.reset_view())

    def run(self) -> None:
        self.root.mainloop()

    def on_close(self) -> None:
        self.fetcher.close()
        self.root.destroy()

    def world_center(self) -> tuple[float, float]:
        return lon_to_world_x(self.center_lon, self.zoom), lat_to_world_y(self.center_lat, self.zoom)

    def set_world_center(self, world_x: float, world_y: float) -> None:
        scale = TILE_SIZE * (1 << self.zoom)
        world_x %= scale
        world_y = clamp(world_y, 0.0, scale)
        self.center_lon = world_x_to_lon(world_x, self.zoom)
        self.center_lat = world_y_to_lat(world_y, self.zoom)

    def on_drag_start(self, event: tk.Event) -> None:
        self.drag_start = (event.x, event.y)
        self.drag_center_world = self.world_center()

    def on_drag_move(self, event: tk.Event) -> None:
        if self.drag_start is None or self.drag_center_world is None:
            return
        start_x, start_y = self.drag_start
        center_world_x, center_world_y = self.drag_center_world
        dx = event.x - start_x
        dy = event.y - start_y
        self.set_world_center(center_world_x - dx, center_world_y - dy)
        self.render()

    def on_mousewheel(self, event: tk.Event) -> None:
        delta = getattr(event, "delta", 0)
        if delta > 0:
            self.zoom_at(event.x, event.y, self.zoom + 1)
        elif delta < 0:
            self.zoom_at(event.x, event.y, self.zoom - 1)

    def on_recenter(self, event: tk.Event) -> None:
        width = max(1, self.canvas.winfo_width())
        height = max(1, self.canvas.winfo_height())
        center_world_x, center_world_y = self.world_center()
        world_x = center_world_x + (event.x - width / 2.0)
        world_y = center_world_y + (event.y - height / 2.0)
        self.set_world_center(world_x, world_y)
        self.render()

    def zoom_at(self, screen_x: int, screen_y: int, new_zoom: int) -> None:
        new_zoom = int(clamp(new_zoom, MIN_ZOOM, MAX_ZOOM))
        if new_zoom == self.zoom:
            return

        width = max(1, self.canvas.winfo_width())
        height = max(1, self.canvas.winfo_height())
        center_world_x, center_world_y = self.world_center()
        before_world_x = center_world_x + (screen_x - width / 2.0)
        before_world_y = center_world_y + (screen_y - height / 2.0)
        before_lon = world_x_to_lon(before_world_x, self.zoom)
        before_lat = world_y_to_lat(before_world_y, self.zoom)

        self.zoom = new_zoom
        target_world_x = lon_to_world_x(before_lon, self.zoom)
        target_world_y = lat_to_world_y(before_lat, self.zoom)
        new_center_world_x = target_world_x - (screen_x - width / 2.0)
        new_center_world_y = target_world_y - (screen_y - height / 2.0)
        self.set_world_center(new_center_world_x, new_center_world_y)
        self.render()

    def reset_view(self) -> None:
        self.zoom = 4
        self.center_lat = 40.7128
        self.center_lon = -74.0060
        self.render()

    def visible_tiles(self) -> list[tuple[int, int, int, int, int]]:
        width = max(1, self.canvas.winfo_width())
        height = max(1, self.canvas.winfo_height())
        center_world_x, center_world_y = self.world_center()
        left = center_world_x - width / 2.0
        top = center_world_y - height / 2.0
        right = center_world_x + width / 2.0
        bottom = center_world_y + height / 2.0

        tiles_per_side = 1 << self.zoom
        min_tile_x = math.floor(left / TILE_SIZE)
        max_tile_x = math.floor(right / TILE_SIZE)
        min_tile_y = math.floor(top / TILE_SIZE)
        max_tile_y = math.floor(bottom / TILE_SIZE)

        visible: list[tuple[int, int, int, int, int]] = []
        for tile_y in range(min_tile_y, max_tile_y + 1):
            if tile_y < 0 or tile_y >= tiles_per_side:
                continue
            for tile_x in range(min_tile_x, max_tile_x + 1):
                wrapped_x = tile_x % tiles_per_side
                screen_x = int(tile_x * TILE_SIZE - left)
                screen_y = int(tile_y * TILE_SIZE - top)
                if screen_x <= width and screen_x + TILE_SIZE >= 0 and screen_y <= height and screen_y + TILE_SIZE >= 0:
                    visible.append((self.zoom, wrapped_x, tile_y, screen_x, screen_y))
        return visible

    def render(self) -> None:
        self.canvas.delete("all")
        visible = self.visible_tiles()
        active_keys = {(z, x, y) for z, x, y, _, _ in visible}
        self.tk_tiles = {key: value for key, value in self.tk_tiles.items() if key in active_keys}
        self.tile_sources = {key: value for key, value in self.tile_sources.items() if key in active_keys}

        for z, x, y, screen_x, screen_y in visible:
            key = (z, x, y)
            photo = self.tk_tiles.get(key)
            if photo is not None:
                self.canvas.create_image(screen_x, screen_y, image=photo, anchor=tk.NW)
            else:
                self.canvas.create_rectangle(screen_x, screen_y, screen_x + TILE_SIZE, screen_y + TILE_SIZE, fill="#1f2937", outline="#334155")
                self.canvas.create_text(screen_x + TILE_SIZE / 2, screen_y + TILE_SIZE / 2, text=f"{z}/{x}/{y}", fill="#94a3b8")
                if key not in self.failed_tiles:
                    self.fetcher.request(z, x, y)

        self.canvas.create_rectangle(10, 10, 370, 68, fill="#020617", outline="#334155")
        mode = "Proxy" if self.proxy_base else "Direct"
        self.canvas.create_text(20, 22, anchor=tk.W, fill="#e2e8f0", text=f"Satellite Viewer  |  {mode}  |  Zoom {self.zoom}")
        self.canvas.create_text(20, 42, anchor=tk.W, fill="#94a3b8", text=f"Lat {self.center_lat:.5f}  Lon {self.center_lon:.5f}")
        self.canvas.create_text(20, 60, anchor=tk.W, fill="#94a3b8", text="Drag to pan, wheel to zoom, right-click to recenter")

        self.update_status()

    def poll_tiles(self) -> None:
        updated = False
        while True:
            try:
                z, x, y, image, from_cache = self.fetcher.completed.get_nowait()
            except queue.Empty:
                break

            key = (z, x, y)
            if image is None:
                self.failed_tiles.add(key)
                self.stats_errors += 1
                updated = True
                continue

            self.tk_tiles[key] = ImageTk.PhotoImage(image)
            self.tile_sources[key] = "cache" if from_cache else "network"
            self.stats_loaded += 1
            if from_cache:
                self.stats_cache_hits += 1
            updated = True

        if updated:
            self.render()

        self.root.after(50, self.poll_tiles)

    def update_status(self) -> None:
        with self.fetcher.pending_lock:
            pending = len(self.fetcher.pending)
        cache_dir = str(self.cache_dir)
        self.status_var.set(
            f"loaded={self.stats_loaded}  cache_hits={self.stats_cache_hits}  errors={self.stats_errors}  pending={pending}  cache={cache_dir}"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Interactive desktop satellite tile viewer.")
    parser.add_argument("--lat", type=float, default=40.7128, help="Starting latitude.")
    parser.add_argument("--lon", type=float, default=-74.0060, help="Starting longitude.")
    parser.add_argument("--zoom", type=int, default=4, help="Starting zoom level.")
    parser.add_argument("--workers", type=int, default=max(8, (os.cpu_count() or 8)), help="Background tile download workers.")
    parser.add_argument("--timeout", type=float, default=20.0, help="Per-request timeout in seconds.")
    parser.add_argument("--proxy-base", default=DEFAULT_PROXY_BASE, help="Proxy base URL. Defaults to the app proxy server.")
    parser.add_argument("--direct", action="store_true", help="Bypass the proxy and fetch tiles directly from Esri.")
    parser.add_argument("--proxy-bypass", action="store_true", help="Alias for --direct.")
    parser.add_argument("--window-size", default="1280x800", help="Initial window size, for example 1280x800.")
    parser.add_argument("--cache-dir", type=Path, default=None, help="Tile cache directory for the viewer.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    viewer = SatelliteViewer(args)
    viewer.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())