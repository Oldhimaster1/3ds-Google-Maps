#!/usr/bin/env python3
"""Build a compact, license-respecting *road geometry* sampling pack for 3DS.

Does NOT download or index Google imagery or panorama locations.
Inputs are locally obtained road GeoJSON or line-delimited GeoJSON features.
Each road is split into roughly equal-length segments, then reservoir sampled.
The 3DS reads fixed-size records with O(1) RAM and O(1) seeking per candidate.

Example:
  python3 tools/build_guess_roads.py --input us_roads.ndjson usa \
      --input eu_roads.ndjson europe --input global_roads.ndjson world \
      --output streetguess_roads.bin --max-records 150000 --seed 5
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import random
import struct
from typing import Iterable

MAGIC = b"SGROAD1\0"
HEADER = struct.Struct("<8sII")
RECORD = struct.Struct("<iiiiI")
FLAGS = {"world": 1, "usa": 1 | 2, "europe": 1 | 4}
ROAD_TYPES = {
    "motorway", "trunk", "primary", "secondary", "tertiary", "residential",
    "unclassified", "living_street", "motorway_link", "trunk_link",
    "primary_link", "secondary_link", "tertiary_link", "road",
}
MAX_LAT = 85.05112878


def norm_lon(lon: float) -> float:
    return (lon + 180.0) % 360.0 - 180.0


def coordinates(line: list) -> Iterable[tuple[float, float]]:
    for coord in line:
        if not isinstance(coord, list) or len(coord) < 2:
            continue
        lon, lat = coord[:2]
        if not isinstance(lon, (int, float)) or not isinstance(lat, (int, float)):
            continue
        lon, lat = float(lon), float(lat)
        if math.isfinite(lat) and math.isfinite(lon) and abs(lat) <= MAX_LAT and abs(lon) <= 180:
            yield lat, lon


def features(path: Path):
    with path.open(encoding="utf-8") as src:
        if path.suffix.lower() in (".ndjson", ".geojsonl", ".jsonl"):
            for line_no, line in enumerate(src, 1):
                if not line.strip():
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError as e:
                    raise ValueError(f"{path}:{line_no}: invalid JSON: {e}") from e
                if obj.get("type") == "Feature":
                    yield obj
        else:
            obj = json.load(src)
            if obj.get("type") != "FeatureCollection":
                raise ValueError(f"{path}: expected a GeoJSON FeatureCollection")
            for feature in obj.get("features", []):
                if isinstance(feature, dict):
                    yield feature


def road_lines(feature: dict):
    props = feature.get("properties") or {}
    highway = props.get("highway")
    if not isinstance(highway, str) or highway not in ROAD_TYPES:
        return
    if props.get("access") in ("private", "no") or props.get("motor_vehicle") == "no":
        return
    geometry = feature.get("geometry") or {}
    typ, coords = geometry.get("type"), geometry.get("coordinates")
    if typ == "LineString" and isinstance(coords, list):
        yield coords
    elif typ == "MultiLineString" and isinstance(coords, list):
        for line in coords:
            if isinstance(line, list):
                yield line


def steps(a: tuple[float,float], b: tuple[float,float], km: float):
    lat0, lon0 = a
    lat1, lon1 = b
    dl = norm_lon(lon1 - lon0)
    dy = lat1 - lat0
    distance = math.hypot(dy * 111.2, dl * 111.2 * math.cos(math.radians((lat0 + lat1)/2)))
    if distance < 0.005 or distance > 1000:
        return
    count = min(500, max(1, math.ceil(distance / km)))
    for i in range(count):
        t0 = i / count
        t1 = (i + 1) / count
        yield (lat0 + dy*t0, norm_lon(lon0 + dl*t0),
               lat0 + dy*t1, norm_lon(lon0 + dl*t1))


def encode_segment(segment: tuple[float,float,float,float], flags: int) -> bytes | None:
    lat0, lon0, lat1, lon1 = segment
    values = [round(v * 1_000_000) for v in (lat0,lon0,lat1,lon1)]
    if values[0] == values[2] and values[1] == values[3]:
        return None
    return RECORD.pack(*values, flags)


def build(inputs: list[tuple[Path, str]], output: Path, max_records: int,
          segment_km: float, seed: int):
    rng = random.Random(seed)
    records: list[bytes] = []
    seen = 0
    for path, region in inputs:
        if not path.is_file():
            raise FileNotFoundError(path)
        valid_features = 0
        for feature in features(path):
            for line in road_lines(feature):
                points = list(coordinates(line))
                if len(points) < 2:
                    continue
                valid_features += 1
                for a, b in zip(points, points[1:]):
                    for segment in steps(a, b, segment_km):
                        record = encode_segment(segment, FLAGS[region])
                        if record is None:
                            continue
                        seen += 1
                        if len(records) < max_records:
                            records.append(record)
                        else:
                            sample = rng.randrange(seen)
                            if sample < max_records:
                                records[sample] = record
        print(f"{path}: processed {valid_features} eligible road lines")
    if not records:
        raise ValueError("No eligible public road segments in input data")
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open("wb") as f:
        f.write(HEADER.pack(MAGIC, len(records), RECORD.size))
        for record in records:
            f.write(record)
    print(f"Wrote {len(records):,} random-access segments ({output.stat().st_size:,} bytes), "
          f"sampled from {seen:,} candidate segments to {output}")
    print("Note: this pack contains independently sourced road geometry, not verified Google Street View coverage.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", nargs=2, action="append", metavar=("GEOJSON", "REGION"),
                        required=True, help="Use world, usa or europe; repeat for multiple files")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-records", type=int, default=150000)
    parser.add_argument("--segment-km", type=float, default=0.7)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.max_records <= 1_000_000:
        parser.error("--max-records must be 1..1000000")
    if not 0.05 <= args.segment_km <= 10:
        parser.error("--segment-km must be 0.05..10")
    inputs = []
    for path, region in args.input:
        if region not in FLAGS:
            parser.error(f"Unknown region: {region}")
        inputs.append((Path(path), region))
    build(inputs, args.output, args.max_records, args.segment_km, args.seed)


if __name__ == "__main__":
    main()