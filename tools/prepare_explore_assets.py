#!/usr/bin/env python3
"""Convert the licensed Explore inputs documented in docs/explore.md."""
import argparse
from pathlib import Path
import zlib

import numpy as np
from PIL import Image


def texture(image, path, size):
    rgb = np.asarray(image.convert("RGB").resize(size, Image.Resampling.LANCZOS),
                     dtype=np.uint16)[::-1]
    packed = ((rgb[:, :, 0] >> 3) << 11) | ((rgb[:, :, 1] >> 2) << 5) | (rgb[:, :, 2] >> 3)
    width, height = size
    y, x = np.indices((height, width))
    morton = ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
              ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))
    tiled = np.empty(width * height, dtype="<u2")
    tiled[(y // 8 * (width // 8) + x // 8) * 64 + morton] = packed
    path.write_bytes(zlib.compress(tiled.tobytes(), level=9))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, ext in (("earth", "png"), ("norfolk", "jpg"), ("georgetown", "jpg")):
        with Image.open(args.inputs / f"{name}.{ext}") as image:
            if image.width != image.height * 2:
                raise ValueError(f"{name} must be a full 2:1 equirectangular raster")
            texture(image, args.output / f"{name}_rgb.bin", (1024, 512))

    with Image.open(args.inputs / "canyon.tif") as image:
        elevations = np.asarray(image, dtype=np.float32)
        if elevations.shape != (128, 128) or not np.all(np.isfinite(elevations)):
            raise ValueError("Expected a valid 128×128 DEM")
        if elevations.min() < 300 or elevations.max() > 4000:
            raise ValueError("Unexpected Grand Canyon elevations")
        # Mesh vertices include both ends of the grid. Sample the same UVs
        # used by the color texture, including the first and last source cells.
        grid = np.linspace(0, 127, 65)
        horizontal = np.stack([np.interp(grid, np.arange(128), row) for row in elevations])
        mesh = np.stack([np.interp(grid, np.arange(128), col) for col in horizontal.T]).T
        (args.output / "canyon_dem.bin").write_bytes(np.rint(mesh).astype("<u2").tobytes())

    # Color bands and north-west lighting are baked from the same real DEM.
    dzdy, dzdx = np.gradient(elevations, 226, 253)
    shade = np.clip((.72 + .46 * dzdx + .52 * dzdy) /
                    np.sqrt(1 + dzdx**2 + dzdy**2), .25, 1)
    colors = ((87, 52, 44), (178, 91, 57), (218, 150, 101),
              (218, 177, 131), (157, 147, 113), (136, 139, 108))
    stops = (450, 950, 1400, 1750, 2180, 2650)
    rgb = np.stack([np.interp(elevations, stops, channel)
                    for channel in zip(*colors)], axis=-1)
    rgb *= (.48 + .65 * shade[:, :, None])
    texture(Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)),
            args.output / "canyon_rgb.bin", (256, 256))


if __name__ == "__main__":
    main()
