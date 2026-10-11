#!/usr/bin/env python3
"""Inspect center samples from odd-size P20 Vulkan captures against CoinGL."""

import json
from pathlib import Path
import sys


if len(sys.argv) != 5:
    raise SystemExit('usage: supersample_probe.py COINGL_128.PPM BGFX_DIR WGPU_384.PPM OUT_JSON')


def read(path):
    magic, size, maximum, raw = Path(path).read_bytes().split(b'\n', 3)
    width, height = map(int, size.split())
    if magic != b'P6' or maximum != b'255' or len(raw) != width*height*3:
        raise ValueError('invalid PPM: ' + str(path))
    return width, height, raw


width, height, reference = read(sys.argv[1])
if (width, height) != (128, 128):
    raise ValueError('CoinGL reference must be 128x128')
results = []
targets = {(36,32), (82,65), (80,79), (59,87)}
bgfx_three = None
for factor in (1, 3, 5, 7, 9, 11, 13, 15):
    extent, rows, high = read(Path(sys.argv[2]) / f'bgfx-vulkan-{factor}x.ppm')
    if (extent, rows) != (128*factor, 128*factor):
        raise ValueError('unexpected supersample extent: ' + str(factor))
    if factor == 3:
        bgfx_three = high
    total = 0
    max_difference = 0
    pixels_over_three = 0
    target_differences = {}
    for y in range(128):
        for x in range(128):
            low_index = (y*128+x)*3
            high_index = ((y*factor+factor//2)*extent +
                          (x*factor+factor//2))*3
            differences = [abs(reference[low_index+c]-high[high_index+c])
                           for c in range(3)]
            total += sum(differences)
            delta = max(differences)
            max_difference = max(max_difference, delta)
            pixels_over_three += delta > 3
            if (x,y) in targets:
                target_differences[f'{x},{y}'] = delta
    results.append({'factor': factor, 'pixels_over3': pixels_over_three,
                    'max_rgb_difference': max_difference,
                    'mae': total/(128*128*3), 'targets': target_differences})

_, _, wgpu_three = read(sys.argv[3])
if len(wgpu_three) != 384*384*3:
    raise ValueError('wgpu capture must be 384x384')
wgpu_pair_exact = bgfx_three == wgpu_three
if not wgpu_pair_exact:
    raise AssertionError('BGFX and wgpu 3x captures differ')
result = {'bgfx_factors': results, 'wgpu_bgfx_3x_exact': wgpu_pair_exact}
Path(sys.argv[4]).write_text(json.dumps(result, indent=2) + '\n')
for row in results:
    print(row['factor'], row['pixels_over3'], row['max_rgb_difference'])
