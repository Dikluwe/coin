#!/usr/bin/env python3
"""Analyze frozen white-scene raster diagnostics (requires numpy and matplotlib)."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('campaign', type=Path)
args = parser.parse_args()
root = args.campaign.resolve()

def ppm(path):
    with path.open('rb') as f:
        assert f.readline() == b'P6\n'
        w, h = map(int, f.readline().split())
        assert f.readline() == b'255\n'
        return np.frombuffer(f.read(), dtype=np.uint8).reshape(h, w, 3)

def raster(triangles, bits, side=256):
    """Independent integer edge equations; top/left ownership, white union."""
    scale = 2**bits
    result = np.zeros((side, side), dtype=bool)
    for triangle in np.floor(triangles*scale+.5).astype(np.int64):
        a, b = triangle[1]-triangle[0], triangle[2]-triangle[0]
        area = a[0]*b[1]-a[1]*b[0]
        if area == 0:
            continue
        if area < 0:
            triangle = triangle[[0, 2, 1]]
        low = np.maximum(np.floor(triangle.min(axis=0)/scale).astype(int), 0)
        high = np.minimum(np.ceil(triangle.max(axis=0)/scale).astype(int), side-1)
        if np.any(high < low):
            continue
        xx, yy = np.meshgrid(np.arange(low[0], high[0]+1)*scale+scale//2,
                             np.arange(low[1], high[1]+1)*scale+scale//2)
        covered = np.ones(xx.shape, dtype=bool)
        for a, b in zip(triangle, np.roll(triangle, -1, axis=0)):
            dx, dy = b-a
            edge = dx*(yy-a[1])-dy*(xx-a[0])
            inclusive = dy < 0 or (dy == 0 and dx > 0)
            covered &= (edge > 0) | ((edge == 0) & inclusive)
        result[low[1]:high[1]+1, low[0]:high[0]+1] |= covered
    return result

camera = root/'amd-vulkan-camera-filled-w1-native'
rows = list(csv.DictReader((root/'amd-vulkan-camera-filled-w1-double/projected.csv').open()))
triangles = np.array([[float(r['x_double']), float(r['y_double'])] for r in rows]).reshape(-1, 3, 2)
coin = ppm(camera/'coin-gl.ppm')[:,:,0] > 0
gpu = ppm(camera/'gpu.ppm')[:,:,0] > 0
precision = []
for bits in [4, 8, 9, 10, 11, 12, 16, 20]:
    mask = raster(triangles, bits)
    precision.append(dict(bits=bits, coin_xor=int(np.count_nonzero(mask^coin)),
                          gpu_xor=int(np.count_nonzero(mask^gpu)), pixels=int(mask.sum())))

witnesses = []
for profile in ['amd-vulkan', 'amd-gl', 'nvidia-vulkan']:
    directory = root/f'{profile}-sphere-lines-w4-native'
    rows = list(csv.DictReader((directory/'polygons.csv').open()))
    edges = list(csv.DictReader((directory/'polygons.csv.edges').open()))
    vertices = list(csv.DictReader((directory/'polygons.csv.vertices').open()))
    matrix = np.array(json.loads((directory/'source-state.json').read_text())['mvp'])
    rings = {}
    for v in vertices:
        rings.setdefault(int(v['polygon']), []).append([float(v[c]) for c in ['x', 'y', 'z']])
    for x, y in [(33, 20), (26, 21), (26, 23)]:
        matching = [r for r in rows if int(r['x'])==x and int(r['y'])==y]
        record = dict(profile=profile, x=x, y=y, winners={}, edges=[])
        for mode in ['common', 'polygon', 'line']:
            hits = [r for r in matching if r[mode+'_covered']=='1']
            winner = min(hits, key=lambda r: float(r[mode+'_depth']))
            record['winners'][mode] = dict(polygon=int(winner['polygon']), depth=float(winner[mode+'_depth']))
        edge_hits = [r for r in edges if int(r['x'])==x and int(r['y'])==y
                     and int(r['polygon']) in [record['winners'][m]['polygon'] for m in ['common', 'polygon']]]
        for r in edge_hits:
            poly, edge = int(r['polygon']), int(r['edge'])
            ring = rings[poly]
            a, b = np.array(ring[(edge-1) % len(ring)] + [1]), np.array(ring[edge] + [1])
            clip = np.array([a @ matrix, b @ matrix])
            ndc = clip[:,:3]/clip[:,3,None]
            screen = np.column_stack(((ndc[:,0]+1)*40, (1-ndc[:,1])*40))
            delta = screen[1]-screen[0]
            major = int(abs(delta[1]) > abs(delta[0]))
            t = (([x+.5,y+.5][major])-screen[0,major])/delta[major]
            z = (ndc[:,2]+1)/2
            r = dict(r)
            r.update(t_major=float(t), endpoint_depths=z.tolist(),
                     extrapolated_major=float(z[0]+t*(z[1]-z[0])),
                     clamped_major=float(z[0]+np.clip(t,0,1)*(z[1]-z[0])))
            record['edges'].append(r)
        witnesses.append(record)

report = dict(precision=precision, witnesses=witnesses,
              limitation='White union ignores occlusion ownership; precision models are not universal driver conformance tests.')
(root/'analysis.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(precision, indent=2))

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig, axes = plt.subplots(1, 3, figsize=(12, 4), constrained_layout=True)
for ax, mask, title in zip(axes, [gpu, coin, gpu^coin],
                         ['AMD Vulkan / CPU', 'CoinGL AMD', '48 differing pixels']):
    ax.imshow(mask, cmap='gray', interpolation='nearest', origin='upper')
    ax.set_title(title); ax.set_xlabel('x'); ax.set_ylabel('y')
fig.savefig(root/'camera-coverage.png', dpi=160); plt.close(fig)
fig, axes = plt.subplots(1, 3, figsize=(12, 4), constrained_layout=True)
directory = root/'amd-vulkan-sphere-lines-w4-native'
for ax, name, title in zip(axes, ['cpu', 'coin-gl', 'replay-gl'],
                         ['Common expansion (CPU)', 'Native CoinGL lines', 'Same triangles, OpenGL']):
    depth = np.fromfile(directory/(name+'.depth-f32'), dtype=np.float32).reshape(80, 80)
    color = ppm(directory/(name+'.ppm'))[:,:,0] > 0
    image = np.ma.masked_where(~color, depth)
    depth_artist = ax.imshow(image, cmap='viridis', interpolation='nearest', origin='upper', vmin=.28, vmax=.40)
    ax.set_xlim(23.5,35.5); ax.set_ylim(26.5,16.5); ax.set_title(title)
    ax.plot([26], [23], 'rx', markersize=10); ax.set_xlabel('x'); ax.set_ylabel('y')
fig.colorbar(depth_artist, ax=axes, label='Depth [0, 1]', shrink=.8)
fig.savefig(root/'sphere-depth.png', dpi=160); plt.close(fig)
files = ['analysis.json', 'camera-coverage.png', 'sphere-depth.png']
(root/'analysis-hashes.json').write_text(json.dumps({f:hashlib.sha256((root/f).read_bytes()).hexdigest()
                                                  for f in files}, indent=2)+'\n')
