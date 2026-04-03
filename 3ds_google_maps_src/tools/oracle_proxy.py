"""Oracle tile + geocode proxy for 3DS homebrew.

Endpoints:
  - /<z>/<x>/<y>.png      -> returns PNG tile (proxied from OSM)
    - /sat/<z>/<x>/<y>.png  -> returns PNG tile (proxied from Esri World Imagery)
    - /geocode?q=<address>  -> returns JSON {"results": [{"name": str, "lat": float, "lon": float}, ...]}
    - /reverse?lat=&lon=    -> returns JSON {"name": str, "lat": float, "lon": float}
    - /pano?lat=<lat>&lon=<lon> -> returns JPEG equirect panorama nearest to that point (Mapillary)
    - /                     -> simple browser dashboard for cache + request stats
    - /stats                -> JSON stats for the dashboard

This lets the 3DS (HTTP-only) talk to a single HTTP server, while this
server performs HTTPS requests to upstream providers.

Run:
    pip install flask requests
  python oracle_proxy.py

Then point the 3DS app at:
  http://<server-ip>:8080
"""

import math
import os
import sys
import threading
import time
from typing import Any, Dict

import requests
from requests.adapters import HTTPAdapter
from flask import Flask, Response, jsonify, request
from urllib3.util.retry import Retry

APP_HOST = os.environ.get("PROXY_HOST", "0.0.0.0")
APP_PORT = int(os.environ.get("PROXY_PORT", "8080"))

TILE_BASE = os.environ.get("TILE_BASE", "https://tile.openstreetmap.org")
SAT_TILE_BASE = os.environ.get(
    "SAT_TILE_BASE",
    "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer",
)
WEB_MERCATOR_HALF_WORLD = 20037508.342789244

# OSRM public routing service (driving profile by default).
OSRM_BASE = os.environ.get("OSRM_BASE", "https://router.project-osrm.org")

# Nominatim usage policy requires a valid User-Agent.
# Set this to something that identifies you/your app (and ideally includes contact info).
UPSTREAM_UA = os.environ.get(
    "UPSTREAM_USER_AGENT",
    "3DS-Google-Maps-Proxy/1.0 (email: micah.lagger@icloud.com)",
)

# Nominatim recommends including a contact email (either as a query param or via headers).
NOMINATIM_EMAIL = os.environ.get("NOMINATIM_EMAIL", "micah.lagger@icloud.com").strip()
UPSTREAM_FROM = os.environ.get("UPSTREAM_FROM", "micah.lagger@icloud.com").strip()

HTTP_TIMEOUT = float(os.environ.get("HTTP_TIMEOUT", "10"))


def _env_bool(name: str, default: bool) -> bool:
    raw = (os.environ.get(name) or "").strip().lower()
    if not raw:
        return default
    return raw in ("1", "true", "yes", "on")


TILE_CACHE_ENABLED = _env_bool("TILE_CACHE_ENABLED", True)
TILE_CACHE_DIR = os.environ.get("TILE_CACHE_DIR", "/tmp/3ds_tile_cache").strip() or "/tmp/3ds_tile_cache"
TILE_CACHE_MAX_FILES = max(100, int(os.environ.get("TILE_CACHE_MAX_FILES", "4000")))
TILE_CACHE_MAX_BYTES = max(8 * 1024 * 1024, int(os.environ.get("TILE_CACHE_MAX_BYTES", str(512 * 1024 * 1024))))
TILE_CACHE_PRUNE_EVERY = max(1, int(os.environ.get("TILE_CACHE_PRUNE_EVERY", "32")))
RECENT_REQUESTS_MAX = max(10, min(100, int(os.environ.get("RECENT_REQUESTS_MAX", "25"))))

# Mapillary Graph API can occasionally be slow from some hosts; use its own
# timeout/retry settings so the rest of the proxy stays snappy.
MAPILLARY_TIMEOUT = float(os.environ.get("MAPILLARY_TIMEOUT", str(max(20.0, HTTP_TIMEOUT))))
MAPILLARY_CONNECT_TIMEOUT = float(os.environ.get("MAPILLARY_CONNECT_TIMEOUT", "4"))
MAPILLARY_RETRIES = int(os.environ.get("MAPILLARY_RETRIES", "2"))

# Mapillary (requires a token). We only use it for 360 panoramas.
MAPILLARY_TOKEN = (os.environ.get("MAPILLARY_ACCESS_TOKEN") or "").strip()
MAPILLARY_GRAPH = os.environ.get("MAPILLARY_GRAPH", "https://graph.mapillary.com")
MAPILLARY_THUMB_SIZE = (os.environ.get("MAPILLARY_THUMB_SIZE") or "1024").strip()

app = Flask(__name__)
_START_TIME = time.time()
_STATS_LOCK = threading.Lock()
_REQUEST_STATS: Dict[str, Any] = {
    "requests_total": 0,
    "tile_requests": 0,
    "tile_cache_hits": 0,
    "tile_cache_misses": 0,
    "tile_upstream_fetches": 0,
    "tile_upstream_errors": 0,
    "tile_upstream_total_ms": 0.0,
    "tile_upstream_last_ms": 0.0,
    "bytes_served": 0,
    "cache_writes": 0,
    "recent_requests": [],
}


def _session_with_retries() -> requests.Session:
    s = requests.Session()
    retry = Retry(
        total=MAPILLARY_RETRIES,
        connect=MAPILLARY_RETRIES,
        read=MAPILLARY_RETRIES,
        status=MAPILLARY_RETRIES,
        backoff_factor=0.4,
        status_forcelist=(429, 500, 502, 503, 504),
        allowed_methods=frozenset(("GET",)),
        raise_on_status=False,
    )
    adapter = HTTPAdapter(max_retries=retry)
    s.mount("https://", adapter)
    s.mount("http://", adapter)
    return s


_HTTP = requests.Session()
_MAP = _session_with_retries()


def _ensure_dir(path: str) -> None:
    os.makedirs(path, exist_ok=True)


def _tile_cache_path(source: str, z: int, x: int, y: int) -> str:
    return os.path.join(TILE_CACHE_DIR, source, str(z), str(x), f"{y}.png")


def _tile_cache_read(source: str, z: int, x: int, y: int) -> bytes | None:
    if not TILE_CACHE_ENABLED:
        return None
    path = _tile_cache_path(source, z, x, y)
    try:
        with open(path, "rb") as f:
            data = f.read()
        now = time.time()
        os.utime(path, (now, now))
        return data
    except OSError:
        return None


def _tile_cache_snapshot() -> Dict[str, int]:
    files = 0
    total_bytes = 0
    if not TILE_CACHE_ENABLED or not os.path.isdir(TILE_CACHE_DIR):
        return {"files": 0, "bytes": 0}

    for root, _, names in os.walk(TILE_CACHE_DIR):
        for name in names:
            path = os.path.join(root, name)
            try:
                st = os.stat(path)
            except OSError:
                continue
            files += 1
            total_bytes += st.st_size
    return {"files": files, "bytes": total_bytes}


def _tile_cache_prune_if_needed() -> None:
    if not TILE_CACHE_ENABLED or not os.path.isdir(TILE_CACHE_DIR):
        return

    entries = []
    total_bytes = 0
    total_files = 0
    for root, _, names in os.walk(TILE_CACHE_DIR):
        for name in names:
            path = os.path.join(root, name)
            try:
                st = os.stat(path)
            except OSError:
                continue
            total_files += 1
            total_bytes += st.st_size
            entries.append((st.st_mtime, st.st_size, path))

    if total_files <= TILE_CACHE_MAX_FILES and total_bytes <= TILE_CACHE_MAX_BYTES:
        return

    entries.sort(key=lambda item: item[0])
    for _, size, path in entries:
        if total_files <= TILE_CACHE_MAX_FILES and total_bytes <= TILE_CACHE_MAX_BYTES:
            break
        try:
            os.remove(path)
        except OSError:
            continue
        total_files -= 1
        total_bytes -= size


def _tile_cache_clear() -> Dict[str, int]:
    removed_files = 0
    removed_dirs = 0
    if not os.path.isdir(TILE_CACHE_DIR):
        return {"files": 0, "dirs": 0}

    for root, dirs, files in os.walk(TILE_CACHE_DIR, topdown=False):
        for name in files:
            path = os.path.join(root, name)
            try:
                os.remove(path)
                removed_files += 1
            except OSError:
                continue
        for name in dirs:
            path = os.path.join(root, name)
            try:
                os.rmdir(path)
                removed_dirs += 1
            except OSError:
                continue
    return {"files": removed_files, "dirs": removed_dirs}


def _tile_cache_write(source: str, z: int, x: int, y: int, content: bytes) -> None:
    if not TILE_CACHE_ENABLED or not content:
        return
    path = _tile_cache_path(source, z, x, y)
    try:
        _ensure_dir(os.path.dirname(path))
        with open(path, "wb") as f:
            f.write(content)
    except OSError as e:
        app.logger.warning("Tile cache write failed for %s: %s", path, e)
        return

    with _STATS_LOCK:
        _REQUEST_STATS["cache_writes"] += 1
        writes = _REQUEST_STATS["cache_writes"]

    if writes % TILE_CACHE_PRUNE_EVERY == 0:
        _tile_cache_prune_if_needed()


def _record_recent_request(status_code: int, content_length: int) -> None:
    endpoint = request.endpoint or "?"
    with _STATS_LOCK:
        _REQUEST_STATS["requests_total"] += 1
        _REQUEST_STATS["bytes_served"] += max(0, content_length)
        recent = _REQUEST_STATS["recent_requests"]
        recent.append(
            {
                "method": request.method,
                "path": request.path,
                "endpoint": endpoint,
                "status": status_code,
                "bytes": max(0, content_length),
                "at": int(time.time()),
            }
        )
        if len(recent) > RECENT_REQUESTS_MAX:
            del recent[:-RECENT_REQUESTS_MAX]


def _reset_stats() -> None:
    with _STATS_LOCK:
        _REQUEST_STATS.clear()
        _REQUEST_STATS.update(
            {
                "requests_total": 0,
                "tile_requests": 0,
                "tile_cache_hits": 0,
                "tile_cache_misses": 0,
                "tile_upstream_fetches": 0,
                "tile_upstream_errors": 0,
                "tile_upstream_total_ms": 0.0,
                "tile_upstream_last_ms": 0.0,
                "bytes_served": 0,
                "cache_writes": 0,
                "recent_requests": [],
            }
        )


def _format_bytes_triplet(num_bytes: int) -> Dict[str, str | int | float]:
    mb = num_bytes / (1024.0 * 1024.0)
    gb = num_bytes / (1024.0 * 1024.0 * 1024.0)
    return {
        "bytes": num_bytes,
        "mb": round(mb, 2),
        "gb": round(gb, 3),
        "text": f"{num_bytes} B / {mb:.2f} MB / {gb:.3f} GB",
    }


def _stats_snapshot() -> Dict[str, Any]:
    with _STATS_LOCK:
        snapshot = dict(_REQUEST_STATS)
        snapshot["recent_requests"] = list(_REQUEST_STATS["recent_requests"])
    snapshot["uptime_s"] = int(max(0, time.time() - _START_TIME))
    snapshot["tile_cache_enabled"] = TILE_CACHE_ENABLED
    snapshot["tile_cache_dir"] = TILE_CACHE_DIR
    snapshot["tile_cache_limits"] = {
        "max_files": TILE_CACHE_MAX_FILES,
        "max_bytes": TILE_CACHE_MAX_BYTES,
    }
    snapshot["tile_cache_usage"] = _tile_cache_snapshot()
    snapshot["bytes_served_human"] = _format_bytes_triplet(int(snapshot.get("bytes_served", 0)))
    total_tile_requests = int(snapshot.get("tile_cache_hits", 0)) + int(snapshot.get("tile_cache_misses", 0))
    hit_rate = (float(snapshot.get("tile_cache_hits", 0)) / float(total_tile_requests) * 100.0) if total_tile_requests > 0 else 0.0
    avg_latency = (float(snapshot.get("tile_upstream_total_ms", 0.0)) / float(snapshot.get("tile_upstream_fetches", 0))) if int(snapshot.get("tile_upstream_fetches", 0)) > 0 else 0.0
    snapshot["tile_hit_rate_pct"] = round(hit_rate, 1)
    snapshot["tile_upstream_avg_ms"] = round(avg_latency, 1)
    snapshot["tile_upstream_last_ms"] = round(float(snapshot.get("tile_upstream_last_ms", 0.0)), 1)
    return snapshot


def _render_dashboard_html(snapshot: Dict[str, Any]) -> str:
    recent_rows = []
    for item in reversed(snapshot["recent_requests"]):
        ts = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(item["at"]))
        recent_rows.append(
            f"<tr><td>{ts}</td><td>{item['method']}</td><td>{item['path']}</td><td>{item['status']}</td><td>{item['bytes']}</td></tr>"
        )
    recent_html = "".join(recent_rows) or "<tr><td colspan='5'>No requests yet.</td></tr>"

    return f"""<!doctype html>
<html lang='en'>
<head>
  <meta charset='utf-8'>
  <title>3DS Maps Proxy</title>
  <style>
    body {{ font-family: sans-serif; margin: 2rem; background: #f5f7fa; color: #1f2937; }}
    h1 {{ margin-bottom: 0.25rem; }}
    .muted {{ color: #6b7280; margin-top: 0; }}
    .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 1rem; margin: 1.5rem 0; }}
    .card {{ background: white; border-radius: 10px; padding: 1rem; box-shadow: 0 1px 3px rgba(0,0,0,0.08); }}
    .label {{ font-size: 0.85rem; color: #6b7280; }}
    .value {{ font-size: 1.4rem; font-weight: 700; margin-top: 0.2rem; }}
    table {{ width: 100%; border-collapse: collapse; background: white; border-radius: 10px; overflow: hidden; box-shadow: 0 1px 3px rgba(0,0,0,0.08); }}
    th, td {{ padding: 0.75rem; border-bottom: 1px solid #e5e7eb; text-align: left; font-size: 0.95rem; }}
    th {{ background: #eef2f7; }}
    code {{ background: #eef2f7; padding: 0.15rem 0.35rem; border-radius: 4px; }}
    a {{ color: #0369a1; }}
        .toolbar {{ display: flex; gap: 0.75rem; align-items: center; flex-wrap: wrap; margin: 1rem 0 1.25rem; }}
        .toggle {{ display: inline-flex; align-items: center; gap: 0.4rem; background: white; padding: 0.6rem 0.8rem; border-radius: 10px; box-shadow: 0 1px 3px rgba(0,0,0,0.08); }}
        .toggle button {{ border: 0; background: #0f766e; color: white; border-radius: 8px; padding: 0.45rem 0.8rem; cursor: pointer; font-weight: 600; }}
        .toggle button.off {{ background: #6b7280; }}
        .toggle select {{ border: 1px solid #d1d5db; border-radius: 8px; padding: 0.35rem 0.5rem; background: white; }}
        .actions {{ display: flex; gap: 0.75rem; flex-wrap: wrap; margin-bottom: 1rem; }}
        .actions button {{ border: 0; background: #1d4ed8; color: white; border-radius: 8px; padding: 0.55rem 0.9rem; cursor: pointer; font-weight: 600; }}
        .actions button.warn {{ background: #b45309; }}
        .actions button.danger {{ background: #b91c1c; }}
        #actionStatus {{ font-size: 0.95rem; color: #374151; }}
  </style>
</head>
<body>
  <h1>3DS Maps Proxy</h1>
    <p class='muted'>Simple status page for the NAS-hosted 3DS proxy. It only shows requests that actually pass through the proxy. Direct OSM and direct Esri satellite tiles will not appear here. JSON stats: <a href='/stats'>/stats</a></p>

    <div class='toolbar'>
        <div class='toggle'>
            <span>Live Updates</span>
            <button id='liveToggle' type='button'>On</button>
            <label for='refreshInterval'>Every</label>
            <select id='refreshInterval'>
                <option value='1000'>1s</option>
                <option value='2000' selected>2s</option>
                <option value='5000'>5s</option>
                <option value='10000'>10s</option>
            </select>
        </div>
        <div class='toggle'>
            <span>Last Update</span>
            <strong id='lastUpdate'>initial</strong>
        </div>
    </div>

    <div class='actions'>
        <button class='warn' id='clearCacheButton' type='button'>Clear Tile Cache</button>
        <button class='danger' id='resetStatsButton' type='button'>Reset Stats Counters</button>
        <span id='actionStatus'>Ready.</span>
    </div>

  <div class='grid'>
        <div class='card'><div class='label'>Uptime</div><div class='value' id='uptimeValue'>{snapshot['uptime_s']}s</div></div>
        <div class='card'><div class='label'>Total Requests</div><div class='value' id='requestsValue'>{snapshot['requests_total']}</div></div>
        <div class='card'><div class='label'>Tile Hits / Misses</div><div class='value' id='hitsMissesValue'>{snapshot['tile_cache_hits']} / {snapshot['tile_cache_misses']}</div></div>
                <div class='card'><div class='label'>Tile Hit Rate</div><div class='value' id='hitRateValue'>{snapshot['tile_hit_rate_pct']}%</div></div>
        <div class='card'><div class='label'>Upstream Tile Fetches</div><div class='value' id='upstreamValue'>{snapshot['tile_upstream_fetches']}</div></div>
                <div class='card'><div class='label'>Tile Latency Avg / Last</div><div class='value' id='latencyValue'>{snapshot['tile_upstream_avg_ms']} / {snapshot['tile_upstream_last_ms']} ms</div></div>
        <div class='card'><div class='label'>Bytes Served</div><div class='value' id='bytesValue'>{snapshot['bytes_served_human']['text']}</div></div>
        <div class='card'><div class='label'>Cache Files / Bytes</div><div class='value' id='cacheUsageValue'>{snapshot['tile_cache_usage']['files']} / {snapshot['tile_cache_usage']['bytes']}</div></div>
  </div>

  <div class='card' style='margin-bottom: 1.5rem;'>
        <div><strong>Tile Cache</strong>: <span id='cacheEnabled'>{'enabled' if snapshot['tile_cache_enabled'] else 'disabled'}</span></div>
        <div><strong>Directory</strong>: <code id='cacheDir'>{snapshot['tile_cache_dir']}</code></div>
        <div><strong>Limits</strong>: <span id='cacheLimits'>{snapshot['tile_cache_limits']['max_files']} files, {snapshot['tile_cache_limits']['max_bytes']} bytes</span></div>
    <div><strong>Street Tile Base</strong>: <code>{TILE_BASE}</code></div>
    <div><strong>Satellite Tile Base</strong>: <code>{SAT_TILE_BASE}</code></div>
  </div>

  <table>
    <thead>
      <tr><th>Time</th><th>Method</th><th>Path</th><th>Status</th><th>Bytes</th></tr>
    </thead>
        <tbody id='recentRequests'>
      {recent_html}
    </tbody>
  </table>

    <script>
        const liveToggle = document.getElementById('liveToggle');
        const refreshInterval = document.getElementById('refreshInterval');
        const lastUpdate = document.getElementById('lastUpdate');
        const actionStatus = document.getElementById('actionStatus');
        let liveEnabled = true;
        let timerId = null;

        function formatTimestamp(unixSeconds) {{
            const d = new Date(unixSeconds * 1000);
            return d.toLocaleString();
        }}

        function setLiveState(enabled) {{
            liveEnabled = enabled;
            liveToggle.textContent = enabled ? 'On' : 'Off';
            liveToggle.classList.toggle('off', !enabled);
            scheduleRefresh();
        }}

        function scheduleRefresh() {{
            if (timerId) {{
                clearTimeout(timerId);
                timerId = null;
            }}
            if (!liveEnabled) return;
            timerId = setTimeout(refreshStats, Number(refreshInterval.value || 2000));
        }}

        function renderRecentRequests(items) {{
            const tbody = document.getElementById('recentRequests');
            if (!Array.isArray(items) || !items.length) {{
                tbody.innerHTML = "<tr><td colspan='5'>No requests yet.</td></tr>";
                return;
            }}
            const rows = items.slice().reverse().map((item) => {{
                return `<tr><td>${{formatTimestamp(item.at)}}</td><td>${{item.method}}</td><td>${{item.path}}</td><td>${{item.status}}</td><td>${{item.bytes}}</td></tr>`;
            }});
            tbody.innerHTML = rows.join('');
        }}

        async function refreshStats() {{
            try {{
                const response = await fetch('/stats', {{ cache: 'no-store' }});
                if (!response.ok) throw new Error(`HTTP ${{response.status}}`);
                const snapshot = await response.json();
                document.getElementById('uptimeValue').textContent = `${{snapshot.uptime_s}}s`;
                document.getElementById('requestsValue').textContent = String(snapshot.requests_total);
                document.getElementById('hitsMissesValue').textContent = `${{snapshot.tile_cache_hits}} / ${{snapshot.tile_cache_misses}}`;
                document.getElementById('hitRateValue').textContent = `${{snapshot.tile_hit_rate_pct}}%`;
                document.getElementById('upstreamValue').textContent = String(snapshot.tile_upstream_fetches);
                document.getElementById('latencyValue').textContent = `${{snapshot.tile_upstream_avg_ms}} / ${{snapshot.tile_upstream_last_ms}} ms`;
                document.getElementById('bytesValue').textContent = String(snapshot.bytes_served_human.text);
                document.getElementById('cacheUsageValue').textContent = `${{snapshot.tile_cache_usage.files}} / ${{snapshot.tile_cache_usage.bytes}}`;
                document.getElementById('cacheEnabled').textContent = snapshot.tile_cache_enabled ? 'enabled' : 'disabled';
                document.getElementById('cacheDir').textContent = snapshot.tile_cache_dir;
                document.getElementById('cacheLimits').textContent = `${{snapshot.tile_cache_limits.max_files}} files, ${{snapshot.tile_cache_limits.max_bytes}} bytes`;
                renderRecentRequests(snapshot.recent_requests);
                lastUpdate.textContent = new Date().toLocaleTimeString();
            }} catch (err) {{
                lastUpdate.textContent = `error: ${{err.message}}`;
            }} finally {{
                scheduleRefresh();
            }}
        }}

        async function postAction(path, confirmMessage) {{
            if (confirmMessage && !window.confirm(confirmMessage)) return;
            actionStatus.textContent = 'Working...';
            try {{
                const response = await fetch(path, {{ method: 'POST' }});
                const payload = await response.json();
                if (!response.ok || payload.ok === false) {{
                    throw new Error(payload.error || `HTTP ${{response.status}}`);
                }}
                actionStatus.textContent = payload.message || 'Done.';
                await refreshStats();
            }} catch (err) {{
                actionStatus.textContent = `Error: ${{err.message}}`;
            }}
        }}

        liveToggle.addEventListener('click', () => setLiveState(!liveEnabled));
        refreshInterval.addEventListener('change', scheduleRefresh);
        document.getElementById('clearCacheButton').addEventListener('click', () => postAction('/admin/clear-tile-cache', 'Clear all cached NAS tiles?'));
        document.getElementById('resetStatsButton').addEventListener('click', () => postAction('/admin/reset-stats', 'Reset dashboard counters?'));
        setLiveState(true);
    </script>
</body>
</html>"""


@app.before_request
def _log_request() -> None:
    # Shows up in the Python console/journal so it's easy to verify the 3DS is hitting the VM.
    app.logger.info("%s %s", request.method, request.full_path)


@app.after_request
def _track_request(response: Response) -> Response:
    content_length = response.calculate_content_length() or 0
    _record_recent_request(response.status_code, content_length)
    return response


@app.get("/")
def dashboard() -> Response:
    snapshot = _stats_snapshot()
    return Response(_render_dashboard_html(snapshot), mimetype="text/html")


@app.get("/stats")
def stats_json() -> Response:
    return jsonify(_stats_snapshot())


@app.post("/admin/clear-tile-cache")
def admin_clear_tile_cache() -> Response:
    removed = _tile_cache_clear()
    return jsonify({"ok": True, "message": f"Cleared {removed['files']} cached tiles.", "removed": removed})


@app.post("/admin/reset-stats")
def admin_reset_stats() -> Response:
    _reset_stats()
    return jsonify({"ok": True, "message": "Stats counters reset."})


def _sanitize_name(value: str, max_len: int = 120) -> str:
    # Keep parsing simple on the 3DS (no JSON escape handling there).
    s = value.replace("\\", "/").replace('"', "'")
    s = " ".join(s.split())
    if len(s) > max_len:
        s = s[: max_len - 3] + "..."
    return s


def _clamp_float(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def _parse_latlon(arg: str) -> tuple[float, float]:
    parts = [p.strip() for p in (arg or "").split(",")]
    if len(parts) != 2:
        raise ValueError("expected 'lat,lon'")
    lat = float(parts[0])
    lon = float(parts[1])
    if not (-90.0 <= lat <= 90.0) or not (-180.0 <= lon <= 180.0):
        raise ValueError("lat/lon out of range")
    return lat, lon


def _maneuver_text(step: Dict[str, Any]) -> str:
    # OSRM doesn't ship full natural-language instructions, so we build a simple one.
    m = step.get("maneuver") or {}
    m_type = str(m.get("type") or "")
    modifier = str(m.get("modifier") or "")
    name = str(step.get("name") or "")

    if m_type == "depart":
        return "Depart"
    if m_type == "arrive":
        return "Arrive"
    if m_type == "roundabout":
        return "Enter roundabout"
    if m_type == "turn":
        if name:
            return f"Turn {modifier} onto {name}".strip()
        return f"Turn {modifier}".strip()
    if name:
        return f"Continue onto {name}".strip()
    return "Continue"


@app.get("/route")
def route():
    # Params:
    #   from=lat,lon
    #   to=lat,lon
    #   profile=driving|walking|cycling (optional)
    from_arg = (request.args.get("from") or "").strip()
    to_arg = (request.args.get("to") or "").strip()
    profile = (request.args.get("profile") or "driving").strip()
    if profile not in ("driving", "walking", "cycling"):
        profile = "driving"

    if not from_arg or not to_arg:
        return jsonify({"error": "missing from/to; expected from=lat,lon&to=lat,lon"}), 400

    try:
        from_lat, from_lon = _parse_latlon(from_arg)
        to_lat, to_lon = _parse_latlon(to_arg)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400

    url = f"{OSRM_BASE.rstrip('/')}/route/v1/{profile}/{from_lon},{from_lat};{to_lon},{to_lat}"
    params = {
        "overview": "full",
        "geometries": "geojson",
        "steps": "true",
    }

    try:
        r = requests.get(url, params=params, headers=_upstream_headers(), timeout=HTTP_TIMEOUT)
    except requests.RequestException as e:
        return jsonify({"error": f"upstream error: {e}"}), 502

    if r.status_code != 200:
        return jsonify({"error": f"upstream status {r.status_code}"}), 502

    try:
        data: Any = r.json()
    except ValueError:
        return jsonify({"error": "invalid upstream json"}), 502

    routes = data.get("routes") if isinstance(data, dict) else None
    if not isinstance(routes, list) or not routes:
        return jsonify({"error": "no route"}), 404

    route0 = routes[0]
    geom = route0.get("geometry") or {}
    coords = geom.get("coordinates")
    if not isinstance(coords, list) or not coords:
        return jsonify({"error": "missing geometry"}), 502

    # Next step
    next_text = "Continue"
    next_dist_m = 0
    legs = route0.get("legs")
    if isinstance(legs, list) and legs:
        steps = (legs[0] or {}).get("steps")
        if isinstance(steps, list) and steps:
            step0 = steps[0] or {}
            next_text = _sanitize_name(_maneuver_text(step0), max_len=60)
            try:
                next_dist_m = int(float(step0.get("distance") or 0.0))
            except (TypeError, ValueError):
                next_dist_m = 0

    # Build a compact "lat,lon;lat,lon;..." string.
    # Cap number of points for 3DS parsing.
    max_pts = int(os.environ.get("ROUTE_MAX_POINTS", "200"))
    max_pts = max(20, min(600, max_pts))
    stride = max(1, int(len(coords) / max_pts))

    pts = []
    for i in range(0, len(coords), stride):
        item = coords[i]
        if not (isinstance(item, list) and len(item) >= 2):
            continue
        lon = float(item[0])
        lat = float(item[1])
        lat = _clamp_float(lat, -90.0, 90.0)
        lon = _clamp_float(lon, -180.0, 180.0)
        pts.append(f"{lat:.5f},{lon:.5f}")
        if len(pts) >= max_pts:
            break

    # Ensure last point is included.
    last = coords[-1]
    if isinstance(last, list) and len(last) >= 2 and pts:
        lon = _clamp_float(float(last[0]), -180.0, 180.0)
        lat = _clamp_float(float(last[1]), -90.0, 90.0)
        last_s = f"{lat:.5f},{lon:.5f}"
        if pts[-1] != last_s:
            pts.append(last_s)

    poly = ";".join(pts)
    return jsonify({"next": next_text, "distance_m": next_dist_m, "poly": poly})


def _upstream_headers() -> Dict[str, str]:
    headers: Dict[str, str] = {
        "User-Agent": UPSTREAM_UA,
        "Accept": "*/*",
    }

    # Some upstream services expect a contact header.
    if UPSTREAM_FROM:
        headers["From"] = UPSTREAM_FROM

    # Optional, but helps some CDNs decide content.
    headers["Accept-Language"] = "en-US,en;q=0.9"

    return headers


def _mapillary_headers() -> Dict[str, str]:
    # Mapillary Graph API auth:
    #   - preferred (Entity API): Authorization: OAuth <token>
    #   - also supported: ?access_token=... (more common for tiles)
    scheme = "OAuth" if MAPILLARY_TOKEN.startswith("MLY|") else "Bearer"
    return {
        "Authorization": f"{scheme} {MAPILLARY_TOKEN}",
        "User-Agent": UPSTREAM_UA,
        "Accept": "application/json",
    }


def _pick_mapillary_thumb_url(image_obj: Dict[str, Any]) -> str:
    # Prefer a predictable thumb size; fall back to whatever exists.
    size = MAPILLARY_THUMB_SIZE
    candidates = [
        f"thumb_{size}_url",
        "thumb_1024_url",
        "thumb_2048_url",
        "thumb_256_url",
    ]
    for k in candidates:
        v = image_obj.get(k)
        if isinstance(v, str) and v.startswith("http"):
            return v
    return ""


@app.get("/pano")
def pano() -> Response:
    # Streams a pano JPEG nearest to the provided lat/lon.
    # Query params:
    #   lat=<float>
    #   lon=<float>
    lat_s = (request.args.get("lat") or "").strip()
    lon_s = (request.args.get("lon") or "").strip()
    radius_s = (request.args.get("radius") or "").strip()
    debug_s = (request.args.get("debug") or "").strip()
    if not lat_s or not lon_s:
        return jsonify({"error": "missing lat/lon"}), 400

    try:
        lat = float(lat_s)
        lon = float(lon_s)
        if not (-90.0 <= lat <= 90.0) or not (-180.0 <= lon <= 180.0):
            raise ValueError("lat/lon out of range")
    except ValueError:
        return jsonify({"error": "invalid lat/lon"}), 400

    if not MAPILLARY_TOKEN:
        app.logger.error("/pano called but MAPILLARY_ACCESS_TOKEN is not set")
        return (
            jsonify(
                {
                    "error": "proxy missing MAPILLARY_ACCESS_TOKEN",
                    "hint": "Set env MAPILLARY_ACCESS_TOKEN to a Mapillary access token and restart the proxy process",
                }
            ),
            500,
        )

    # Find a pano image near the point.
    # Mapillary's Graph Entity API supports only bbox-based spatial search.
    # Docs note the bbox area must be smaller than 0.01 degrees square.
    radius_m = 3000
    if radius_s:
        try:
            radius_m = int(float(radius_s))
        except ValueError:
            radius_m = 3000
    radius_m = max(50, min(8000, radius_m))

    debug = debug_s in ("1", "true", "yes", "on")

    # Convert meters -> degrees and clamp to the API's bbox size constraint.
    # bbox format: minLon,minLat,maxLon,maxLat
    lat_deg_per_m = 1.0 / 111_320.0
    # Avoid division by zero at the poles.
    lon_deg_per_m = 1.0 / max(1e-9, (111_320.0 * math.cos(math.radians(lat))))

    lat_delta = radius_m * lat_deg_per_m
    lon_delta = radius_m * lon_deg_per_m

    # Enforce bbox side length <= 0.01 degrees (delta <= 0.005)
    bbox_cap_delta = 0.005
    capped = False
    if lat_delta > bbox_cap_delta:
        lat_delta = bbox_cap_delta
        capped = True
    if lon_delta > bbox_cap_delta:
        lon_delta = bbox_cap_delta
        capped = True

    min_lon = lon - lon_delta
    max_lon = lon + lon_delta
    min_lat = lat - lat_delta
    max_lat = lat + lat_delta

    search_url = f"{MAPILLARY_GRAPH.rstrip('/')}/images"
    params = {
        "bbox": f"{min_lon:.6f},{min_lat:.6f},{max_lon:.6f},{max_lat:.6f}",
        "is_pano": "true",
        "limit": "2000",
        "fields": "id,is_pano,camera_type,thumb_256_url,thumb_1024_url,thumb_2048_url,thumb_original_url",
    }

    # For client tokens (dashboard tokens usually start with "MLY|"), Mapillary also
    # supports passing the token as a query param. This improves compatibility with
    # different deployments and proxies.
    if MAPILLARY_TOKEN.startswith("MLY|"):
        params["access_token"] = MAPILLARY_TOKEN

    try:
        r = _MAP.get(
            search_url,
            params=params,
            headers=_mapillary_headers(),
            timeout=(MAPILLARY_CONNECT_TIMEOUT, MAPILLARY_TIMEOUT),
        )
    except requests.RequestException as e:
        return (
            jsonify(
                {
                    "error": f"upstream error: {e}",
                    "hint": "Mapillary timed out; try increasing MAPILLARY_TIMEOUT (seconds) or check VM egress/DNS",
                    "timeout_s": MAPILLARY_TIMEOUT,
                }
            ),
            502,
        )

    if r.status_code != 200:
        snippet = (r.text or "").strip().replace("\n", " ")
        if len(snippet) > 180:
            snippet = snippet[:177] + "..."
        app.logger.warning("Mapillary search status=%s body=%s", r.status_code, snippet)
        if r.status_code in (401, 403):
            return (
                jsonify(
                    {
                        "error": f"upstream status {r.status_code}",
                        "hint": "Mapillary token missing/invalid/expired or lacks permissions",
                    }
                ),
                502,
            )
        return jsonify({"error": f"upstream status {r.status_code}"}), 502

    try:
        data: Any = r.json()
    except ValueError:
        return jsonify({"error": "invalid upstream json"}), 502

    items = data.get("data") if isinstance(data, dict) else None
    if not isinstance(items, list) or not items:
        if debug:
            return (
                jsonify(
                    {
                        "error": "no images nearby",
                        "radius_m": radius_m,
                        "bbox": params.get("bbox"),
                        "bbox_capped": capped,
                        "count": 0,
                    }
                ),
                404,
            )
        return jsonify({"error": "no pano nearby"}), 404

    # With is_pano=true, every result should be pano; still filter defensively.
    img0: Dict[str, Any] | None = None
    sample = []
    for it in items:
        if not isinstance(it, dict):
            continue
        if debug and len(sample) < 8:
            sample.append({"id": it.get("id"), "is_pano": it.get("is_pano"), "camera_type": it.get("camera_type")})
        if it.get("is_pano") is True:
            img0 = it
            break

    if img0 is None:
        if debug:
            return (
                jsonify(
                    {
                        "error": "no pano nearby",
                        "radius_m": radius_m,
                        "bbox": params.get("bbox"),
                        "bbox_capped": capped,
                        "count": len(items),
                        "sample": sample,
                    }
                ),
                404,
            )
        return jsonify({"error": "no pano nearby"}), 404

    thumb_url = _pick_mapillary_thumb_url(img0)
    if not thumb_url:
        if debug:
            return jsonify({"error": "missing pano thumb url", "radius_m": radius_m, "picked": {"id": img0.get("id"), "is_pano": img0.get("is_pano")}}), 502
        return jsonify({"error": "missing pano thumb url"}), 502

    if debug:
        return jsonify(
            {
                "ok": True,
                "radius_m": radius_m,
                "bbox": params.get("bbox"),
                "bbox_capped": capped,
                "picked": {"id": img0.get("id"), "is_pano": img0.get("is_pano")},
                "thumb_url": thumb_url,
                "count": len(items),
                "sample": sample,
            }
        )

    try:
        img_r = _MAP.get(
            thumb_url,
            headers={"User-Agent": UPSTREAM_UA, "Accept": "image/*"},
            timeout=(MAPILLARY_CONNECT_TIMEOUT, MAPILLARY_TIMEOUT),
        )
    except requests.RequestException as e:
        return (
            jsonify(
                {
                    "error": f"upstream image error: {e}",
                    "hint": "Mapillary image download timed out; increase MAPILLARY_TIMEOUT or try a smaller thumb size",
                    "timeout_s": MAPILLARY_TIMEOUT,
                }
            ),
            502,
        )

    if img_r.status_code != 200 or not img_r.content:
        return jsonify({"error": f"upstream image status {img_r.status_code}"}), 502

    resp = Response(img_r.content, status=200, mimetype="image/jpeg")
    resp.headers["Cache-Control"] = "public, max-age=86400"
    return resp


@app.get("/<int:z>/<int:x>/<int:y>.png")
def tile(z: int, x: int, y: int) -> Response:
    with _STATS_LOCK:
        _REQUEST_STATS["tile_requests"] += 1

    cached = _tile_cache_read("street", z, x, y)
    if cached is not None:
        with _STATS_LOCK:
            _REQUEST_STATS["tile_cache_hits"] += 1
        resp = Response(cached, status=200, mimetype="image/png")
        resp.headers["Cache-Control"] = "public, max-age=86400"
        resp.headers["X-Proxy-Cache"] = "HIT"
        return resp

    with _STATS_LOCK:
        _REQUEST_STATS["tile_cache_misses"] += 1

    url = f"{TILE_BASE.rstrip('/')}/{z}/{x}/{y}.png"
    fetch_started = time.time()
    try:
        with _STATS_LOCK:
            _REQUEST_STATS["tile_upstream_fetches"] += 1
        r = _HTTP.get(url, headers=_upstream_headers(), timeout=HTTP_TIMEOUT)
    except requests.RequestException as e:
        elapsed_ms = (time.time() - fetch_started) * 1000.0
        with _STATS_LOCK:
            _REQUEST_STATS["tile_upstream_errors"] += 1
            _REQUEST_STATS["tile_upstream_last_ms"] = elapsed_ms
            _REQUEST_STATS["tile_upstream_total_ms"] += elapsed_ms
        return Response(f"Upstream error: {e}\n", status=502, mimetype="text/plain")

    elapsed_ms = (time.time() - fetch_started) * 1000.0
    with _STATS_LOCK:
        _REQUEST_STATS["tile_upstream_last_ms"] = elapsed_ms
        _REQUEST_STATS["tile_upstream_total_ms"] += elapsed_ms

    if r.status_code != 200:
        with _STATS_LOCK:
            _REQUEST_STATS["tile_upstream_errors"] += 1
        return Response(
            f"Upstream returned {r.status_code}\n",
            status=502,
            mimetype="text/plain",
        )

    _tile_cache_write("street", z, x, y, r.content)

    resp = Response(r.content, status=200, mimetype="image/png")
    # Conservative caching; tune if you want.
    resp.headers["Cache-Control"] = "public, max-age=86400"
    resp.headers["X-Proxy-Cache"] = "MISS"
    return resp


@app.get("/sat/<int:z>/<int:x>/<int:y>.png")
def satellite_tile(z: int, x: int, y: int) -> Response:
    with _STATS_LOCK:
        _REQUEST_STATS["tile_requests"] += 1

    cached = _tile_cache_read("sat", z, x, y)
    if cached is not None:
        with _STATS_LOCK:
            _REQUEST_STATS["tile_cache_hits"] += 1
        resp = Response(cached, status=200, mimetype="image/png")
        resp.headers["Cache-Control"] = "public, max-age=86400"
        resp.headers["X-Proxy-Cache"] = "HIT"
        return resp

    with _STATS_LOCK:
        _REQUEST_STATS["tile_cache_misses"] += 1

    tiles = float(1 << z)
    world = WEB_MERCATOR_HALF_WORLD * 2.0
    min_x = (x / tiles) * world - WEB_MERCATOR_HALF_WORLD
    max_x = ((x + 1) / tiles) * world - WEB_MERCATOR_HALF_WORLD
    max_y = WEB_MERCATOR_HALF_WORLD - (y / tiles) * world
    min_y = WEB_MERCATOR_HALF_WORLD - ((y + 1) / tiles) * world

    url = (
        f"{SAT_TILE_BASE.rstrip('/')}/export?"
        f"bbox={min_x:.6f},{min_y:.6f},{max_x:.6f},{max_y:.6f}"
        f"&bboxSR=102100&imageSR=102100&size=256,256"
        f"&format=png32&transparent=false&f=image"
    )
    fetch_started = time.time()
    try:
        with _STATS_LOCK:
            _REQUEST_STATS["tile_upstream_fetches"] += 1
        r = _HTTP.get(url, headers=_upstream_headers(), timeout=HTTP_TIMEOUT)
    except requests.RequestException as e:
        elapsed_ms = (time.time() - fetch_started) * 1000.0
        with _STATS_LOCK:
            _REQUEST_STATS["tile_upstream_errors"] += 1
            _REQUEST_STATS["tile_upstream_last_ms"] = elapsed_ms
            _REQUEST_STATS["tile_upstream_total_ms"] += elapsed_ms
        return Response(f"Upstream error: {e}\n", status=502, mimetype="text/plain")

    elapsed_ms = (time.time() - fetch_started) * 1000.0
    with _STATS_LOCK:
        _REQUEST_STATS["tile_upstream_last_ms"] = elapsed_ms
        _REQUEST_STATS["tile_upstream_total_ms"] += elapsed_ms

    if r.status_code != 200 or not r.content:
        with _STATS_LOCK:
            _REQUEST_STATS["tile_upstream_errors"] += 1
        return Response(
            f"Upstream returned {r.status_code}\n",
            status=502,
            mimetype="text/plain",
        )

    _tile_cache_write("sat", z, x, y, r.content)

    resp = Response(r.content, status=200, mimetype="image/png")
    resp.headers["Cache-Control"] = "public, max-age=86400"
    resp.headers["X-Proxy-Cache"] = "MISS"
    return resp


@app.get("/geocode")
def geocode():
    q = (request.args.get("q") or "").strip()
    if not q:
        return jsonify({"error": "missing query 'q'"}), 400

    # Nominatim will often reject generic or missing User-Agent/contact.
    if "you@example.com" in UPSTREAM_UA:
        return (
            jsonify(
                {
                    "error": "proxy is missing a real UPSTREAM_USER_AGENT; set env UPSTREAM_USER_AGENT to include contact info",
                    "example": "export UPSTREAM_USER_AGENT=\"3DS-Maps-Proxy/1.0 (email: you@domain.com)\"",
                }
            ),
            500,
        )

    # Nominatim search API
    url = "https://nominatim.openstreetmap.org/search"
    params = {
        "q": q,
        "format": "json",
        "limit": 5,
    }

    if NOMINATIM_EMAIL:
        params["email"] = NOMINATIM_EMAIL

    try:
        r = requests.get(url, params=params, headers=_upstream_headers(), timeout=HTTP_TIMEOUT)
    except requests.RequestException as e:
        return jsonify({"error": f"upstream error: {e}"}), 502

    if r.status_code != 200:
        # 403 is commonly a policy/User-Agent issue with Nominatim.
        if r.status_code == 403:
            return (
                jsonify(
                    {
                        "error": "upstream status 403 (Nominatim rejected request)",
                        "hint": "Set UPSTREAM_USER_AGENT to a real UA with contact info; optionally set NOMINATIM_EMAIL and/or UPSTREAM_FROM",
                    }
                ),
                502,
            )
        return jsonify({"error": f"upstream status {r.status_code}"}), 502

    try:
        data: Any = r.json()
    except ValueError:
        return jsonify({"error": "invalid upstream json"}), 502

    if not isinstance(data, list) or not data:
        return jsonify({"error": "no results"}), 404

    results = []
    for item in data[:5]:
        try:
            lat = float(item.get("lat"))
            lon = float(item.get("lon"))
        except (TypeError, ValueError):
            continue

        name = item.get("display_name") or item.get("name") or q
        if not isinstance(name, str):
            name = q
        results.append({"name": _sanitize_name(name), "lat": lat, "lon": lon})

    if not results:
        return jsonify({"error": "no valid results"}), 404

    return jsonify({"results": results})


@app.get("/reverse")
def reverse_geocode():
    lat_s = (request.args.get("lat") or "").strip()
    lon_s = (request.args.get("lon") or "").strip()
    if not lat_s or not lon_s:
        return jsonify({"error": "missing lat/lon"}), 400

    try:
        lat = float(lat_s)
        lon = float(lon_s)
        if not (-90.0 <= lat <= 90.0) or not (-180.0 <= lon <= 180.0):
            raise ValueError("lat/lon out of range")
    except ValueError:
        return jsonify({"error": "invalid lat/lon"}), 400

    if "you@example.com" in UPSTREAM_UA:
        return (
            jsonify(
                {
                    "error": "proxy is missing a real UPSTREAM_USER_AGENT; set env UPSTREAM_USER_AGENT to include contact info",
                    "example": "export UPSTREAM_USER_AGENT=\"3DS-Maps-Proxy/1.0 (email: you@domain.com)\"",
                }
            ),
            500,
        )

    url = "https://nominatim.openstreetmap.org/reverse"
    params = {
        "lat": f"{lat:.6f}",
        "lon": f"{lon:.6f}",
        "format": "json",
        "zoom": "18",
        "addressdetails": "0",
    }

    if NOMINATIM_EMAIL:
        params["email"] = NOMINATIM_EMAIL

    try:
        r = _HTTP.get(url, params=params, headers=_upstream_headers(), timeout=HTTP_TIMEOUT)
    except requests.RequestException as e:
        return jsonify({"error": f"upstream error: {e}"}), 502

    if r.status_code != 200:
        if r.status_code == 403:
            return (
                jsonify(
                    {
                        "error": "upstream status 403 (Nominatim rejected request)",
                        "hint": "Set UPSTREAM_USER_AGENT to a real UA with contact info; optionally set NOMINATIM_EMAIL and/or UPSTREAM_FROM",
                    }
                ),
                502,
            )
        return jsonify({"error": f"upstream status {r.status_code}"}), 502

    try:
        data: Any = r.json()
    except ValueError:
        return jsonify({"error": "invalid upstream json"}), 502

    if not isinstance(data, dict):
        return jsonify({"error": "invalid reverse response"}), 502

    name = data.get("display_name") or data.get("name") or data.get("licence") or "Unknown location"
    if not isinstance(name, str):
        name = "Unknown location"

    return jsonify({"name": _sanitize_name(name), "lat": lat, "lon": lon})


if __name__ == "__main__":
    # Print interpreter version on startup (helps debug VM Python versions).
    print(f"Starting proxy with Python {sys.version}")
    app.run(host=APP_HOST, port=APP_PORT, threaded=True)
