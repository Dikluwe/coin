#!/usr/bin/env python3
"""Audit frame-five edge coverage across AMD/NVIDIA and 1x/3x captures."""

import argparse
import json
import math
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument('--projected', required=True, type=Path)
parser.add_argument('--matrix', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()

vertices = {}
for line in args.projected.read_text().splitlines():
    fields = line.split()
    if len(fields) != 7:
        raise ValueError('invalid projected cube')
    vertices[tuple(map(int, fields[:5]))] = tuple(map(float, fields[5:]))

edge = set()
edge_x = None
for iy in (1, 4, 7, 10, 13):
    upper = vertices[8, iy, 1, 1, 1]
    lower = vertices[8, iy, 1, -1, 1]
    if abs(upper[0] - lower[0]) > 1e-9:
        raise AssertionError('expected vertical edge')
    if edge_x is None:
        edge_x = upper[0]
    elif abs(edge_x - upper[0]) > 1e-9:
        raise AssertionError('edges have different x coordinates')
    edge.update((195, y) for y in range(256)
                if min(upper[1], lower[1]) < y + .5 < max(upper[1], lower[1]))
if len(edge) != 44:
    raise AssertionError(f'projected edge has {len(edge)} samples, expected 44')


def pixels(path):
    header, extent, maximum, data = path.read_bytes().split(b'\n', 3)
    if (header, extent, maximum, len(data)) != (b'P6', b'256 256', b'255', 256*256*3):
        raise ValueError(f'invalid PPM: {path}')
    return data


rows = []
errors = {}
for gpu in ('amd', 'nvidia'):
    for factor in (1, 3):
        folder = args.matrix / f'{gpu}-wgpu-vulkan-{factor}x'
        actual = pixels(folder / 'perspective-CoinGL-5-actual.ppm')
        reference = pixels(folder / 'perspective-CoinGL-5-reference.ppm')
        covered = {}
        for label, image in (('coinrender', actual), ('coingl', reference)):
            covered[label] = sum(image[(y*256+x)*3:(y*256+x)*3+3] != bytes(3)
                                 for x, y in edge)
        differences = set()
        for y in range(256):
            for x in range(256):
                index = (y*256+x)*3
                if max(abs(actual[index+c]-reference[index+c]) for c in range(3)) > 3:
                    differences.add((x,y))
        errors[gpu, factor] = differences
        row = dict(gpu=gpu, factor=factor, edge_pixels=len(edge),
                   covered=covered, pixels_over3=len(differences),
                   column_195_over3=sum(x == 195 for x, _ in differences))
        rows.append(row)
        print(row)

expected = {('amd', 1): (0, 44), ('amd', 3): (44, 44),
            ('nvidia', 1): (0, 0), ('nvidia', 3): (44, 0)}
for row in rows:
    pair = row['covered']['coinrender'], row['covered']['coingl']
    if pair != expected[row['gpu'], row['factor']]:
        raise AssertionError(f'unexpected edge coverage: {row}')
if not edge <= errors['amd', 1] or not edge <= errors['nvidia', 3]:
    raise AssertionError('projected edge does not match the observed errors')

result = dict(edge_x_logical=edge_x, edge_pixels=len(edge), rows=rows,
              failure_overlap=len(errors['amd', 1] & errors['nvidia', 3]),
              nvidia_only_errors=sorted(errors['nvidia', 3] - errors['amd', 1]),
              quantized_edge_1x=math.floor(edge_x*256+.5)/256,
              quantized_edge_3x=math.floor(edge_x*3*256+.5)/256)
args.output.write_text(json.dumps(result, indent=2) + '\n')
