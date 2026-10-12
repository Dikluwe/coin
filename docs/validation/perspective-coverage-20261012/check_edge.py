#!/usr/bin/env python3
"""Match the five projected right edges to the frame-five coverage pixels."""

import math
from pathlib import Path
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: check_edge.py PROJECTED_CUBES ACTUAL_PPM COINGL_PPM")

vertices = {}
for line in Path(sys.argv[1]).read_text().splitlines():
    fields = line.split()
    if len(fields) != 7:
        raise ValueError("invalid projected cube line")
    ix, iy, sx, sy, sz = map(int, fields[:5])
    vertices[ix, iy, sx, sy, sz] = tuple(map(float, fields[5:]))


def ppm(path):
    header, extent, maximum, pixels = Path(path).read_bytes().split(b"\n", 3)
    if (header, extent, maximum, len(pixels)) != (b"P6", b"256 256", b"255", 256 * 256 * 3):
        raise ValueError("unexpected PPM format: " + path)
    return pixels


actual, reference = ppm(sys.argv[2]), ppm(sys.argv[3])
missing = set()
for y in range(256):
    index = (y * 256 + 195) * 3
    gpu, gl = actual[index:index + 3], reference[index:index + 3]
    if gpu == bytes(3) and gl != bytes(3) and max(abs(a - b) for a, b in zip(gpu, gl)) > 3:
        missing.add((195, y))

expected = set()
for iy in (1, 4, 7, 10, 13):
    upper = vertices[8, iy, 1, 1, 1]
    lower = vertices[8, iy, 1, -1, 1]
    if abs(upper[0] - lower[0]) > 1e-9:
        raise AssertionError("expected vertical right edge")
    x = upper[0]
    coarse = math.floor(x * 256 + .5) / 256
    fine = math.floor(x * 4096 + .5) / 4096
    if not (coarse == 195.5 and fine > 195.5):
        raise AssertionError("the two raster grids do not straddle the sample center")
    expected.update((195, y) for y in range(256)
                    if min(upper[1], lower[1]) < y + .5 < max(upper[1], lower[1]))
    print(f"cube (8,{iy}) edge_x={x:.9f} coarse={coarse:.9f} fine={fine:.9f}")

if missing != expected:
    raise AssertionError(f"projected/observed mismatch: expected={len(expected)} observed={len(missing)}")
print(f"matched {len(expected)}/{len(missing)} x=195 coverage differences")
