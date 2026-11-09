"""Validate the exact data consumed by the renderer without third-party modules."""
from pathlib import Path
import struct
import zlib

data = Path(__file__).resolve().parents[1] / "data"

for name, width, height in (("earth", 1024, 512), ("canyon", 256, 256),
                            ("norfolk", 1024, 512), ("georgetown", 1024, 512)):
    texture = zlib.decompress((data / f"{name}_rgb.bin").read_bytes())
    assert len(texture) == width * height * 2, name
    assert len(set(texture)) > 100, name

height_grid = struct.unpack("<4225H", (data / "canyon_dem.bin").read_bytes())
assert 700 <= min(height_grid) <= 800 and 2400 <= max(height_grid) <= 2700
assert len(set(height_grid)) > 1000
assert (data / "norfolk_rgb.bin").read_bytes() != (data / "georgetown_rgb.bin").read_bytes()
print("Explore texture sizes, distinct street stops and terrain grid passed.")
