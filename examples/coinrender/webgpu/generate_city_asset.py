#!/usr/bin/env python3
"""Compact little-endian city records from the same authored fields as the IV generator."""
import argparse
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from generate_large_scene import PALETTE, building_records, city_span

HEADER = struct.Struct('<8s6I6f')
RECORD = struct.Struct('<6fI')

def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]

def write_asset(output, grid, seed=136):
    if not 1 <= grid <= 1000:
        raise ValueError('grid must be between 1 and 1000')
    palette = PALETTE + [(0.16, 0.21, 0.23)]
    span = float(f'{city_span(grid) + 6:.2f}')
    bounds = [float('inf')] * 3 + [float('-inf')] * 3
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as out:
        out.write(bytes(HEADER.size))
        for color in palette:
            out.write(struct.pack('<3f', *color))
        def append(translation, scale, color):
            values = [f32(float(v)) for v in (*translation, *scale)]
            for axis in range(3):
                half = f32(values[axis + 3] * 0.5)
                bounds[axis] = min(bounds[axis], f32(values[axis] - half))
                bounds[axis + 3] = max(bounds[axis + 3], f32(values[axis] + half))
            out.write(RECORD.pack(*values, color))
        append((0, -0.25, 0), (span, 0.5, span), 8)
        for record in building_records(grid, seed):
            append(record['translation'], record['scale'], record['index'] % 8)
        out.seek(0)
        out.write(HEADER.pack(b'CRCTY001', grid, seed, grid * grid, grid * grid + 1,
                              RECORD.size, len(palette), *bounds))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--grid', type=int, required=True)
    parser.add_argument('--seed', type=int, default=136)
    args = parser.parse_args()
    write_asset(args.output, args.grid, args.seed)
