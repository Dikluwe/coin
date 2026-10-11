#!/usr/bin/env python3
"""Compare the two observed AMD subpixel grids at the four P20 samples."""

import json
import math
from pathlib import Path
import sys


if len(sys.argv) != 4:
    raise SystemExit('usage: coverage_model.py PROJECTED_TXT SINGLE_QUADS_DIR OUT_JSON')

projected = {}
for line in Path(sys.argv[1]).read_text().splitlines():
    fields = line.split()
    if len(fields) != 5:
        continue
    quad, vertex, x, y, _w = fields
    projected.setdefault(int(quad), {})[int(vertex)] = (float(x), float(y))


def coverage(vertices, sample, ticks):
    snapped = [(math.floor(x*ticks + .5)/ticks,
                math.floor(y*ticks + .5)/ticks) for x, y in vertices]
    triangles = ((0, 1, 2), (0, 2, 3))
    edge_sets = []
    for tri in triangles:
        distances = []
        for a, b in zip(tri, tri[1:] + tri[:1]):
            x0, y0 = snapped[a]
            x1, y1 = snapped[b]
            length = math.hypot(x1-x0, y1-y0)
            if length == 0:
                raise ValueError('degenerate projected edge')
            cross = (x1-x0)*(sample[1]-y0) - (y1-y0)*(sample[0]-x0)
            distances.append(cross/length)
        edge_sets.append(distances)
    # None of these samples lies exactly on a snapped edge. Top-left tie
    # ownership therefore does not enter the four comparisons.
    if any(distance == 0 for edges in edge_sets for distance in edges):
        raise ValueError('sample on snapped edge; tie rule required')
    inside = any(all(d > 0 for d in edges) or all(d < 0 for d in edges)
                 for edges in edge_sets)
    return inside, min(abs(d) for edges in edge_sets for d in edges)


def pixel(path, x, y):
    header, extent, maximum, raw = path.read_bytes().split(b'\n', 3)
    if (header, extent, maximum, len(raw)) != (b'P6', b'128 128', b'255', 128*128*3):
        raise ValueError('unexpected PPM: ' + str(path))
    index = (y*128+x)*3
    return list(raw[index:index+3])


directory = Path(sys.argv[2])
samples = ((0, 1, 36, 32, True, False),
           (24, 24, 82, 65, True, False),
           (30, 30, 59, 87, True, False),
           (39, 39, 80, 79, False, True))
results = []
for quad, image_quad, x, y, expected_256, expected_4096 in samples:
    vertices = [projected[quad][i] for i in range(4)]
    sample = (x+.5, y+.5)
    coarse, coarse_distance = coverage(vertices, sample, 256)
    fine, fine_distance = coverage(vertices, sample, 4096)
    gl = pixel(directory/f'quad-{image_quad:02d}-gl.ppm', x, y)
    vulkan = pixel(directory/f'quad-{image_quad:02d}-bgfx.ppm', x, y)
    row = {'quad': quad, 'pixel': [x, y], 'sample_center': list(sample),
           'coverage_256': coarse, 'coverage_4096': fine,
           'nearest_edge_256_px': coarse_distance,
           'nearest_edge_4096_px': fine_distance,
           'single_quad_coingl_rgb': gl, 'single_quad_vulkan_rgb': vulkan,
           'pixel_rgb_max_delta': max(abs(a-b) for a,b in zip(gl,vulkan))}
    if (coarse, fine) != (expected_256, expected_4096) or row['pixel_rgb_max_delta'] <= 3:
        raise AssertionError('model and observed edge disagree: ' + str(row))
    results.append(row)

Path(sys.argv[3]).write_text(json.dumps(results, indent=2) + '\n')
for row in results:
    print(row['quad'], row['pixel'], row['coverage_256'],
          row['coverage_4096'], row['pixel_rgb_max_delta'])
