#!/usr/bin/env python3
"""Measure the offscreen render/readback cost of 256px versus 768px targets."""

import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--bgfx-build', required=True, type=Path)
parser.add_argument('--wgpu-build', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
scene = Path(__file__).resolve().parents[1] / 'hardware-surfaces-linux-20261007/fixtures/transparent-overlap.iv'

rows = []
for gpu in ('amd', 'nvidia'):
    for backend in ('bgfx', 'wgpu'):
        build = args.bgfx_build if backend == 'bgfx' else args.wgpu_build
        for repetition in range(3):
            for factor in ((1, 3) if repetition != 1 else (3, 1)):
                name = f'{gpu}-{backend}-{factor}x-run{repetition}'
                env = os.environ.copy()
                icd = 'radeon' if gpu == 'amd' else 'nvidia'
                env.update(DISPLAY=':0', LD_LIBRARY_PATH=str(build / 'lib'),
                           VK_DRIVER_FILES=f'/usr/share/vulkan/icd.d/{icd}_icd.json',
                           VK_ICD_FILENAMES=f'/usr/share/vulkan/icd.d/{icd}_icd.json',
                           __GLX_VENDOR_LIBRARY_NAME='mesa' if gpu == 'amd' else 'nvidia',
                           __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/' +
                               ('50_mesa.json' if gpu == 'amd' else '10_nvidia.json'),
                           COIN_GLX_PIXMAP_DIRECT_RENDERING='1',
                           COIN_GLXGLUE_NO_PBUFFERS='1' if gpu == 'amd' else '0',
                           COIN_BGFX_RENDERER='vulkan', WGPU_BACKEND='vulkan')
                if gpu == 'nvidia':
                    env['__NV_PRIME_RENDER_OFFLOAD'] = '1'
                else:
                    env.pop('__NV_PRIME_RENDER_OFFLOAD', None)
                command = ['/usr/bin/time', '-f', 'MAX_RSS_KIB=%M',
                           str(build / 'bin/coin_render_gl_benchmark'),
                           '--backend', backend, '--scene', str(scene),
                           '--size', '256', '--center-sample-scale', str(factor),
                           '--warmup', '8', '--frames', '60']
                try:
                    process = subprocess.run(command, env=env, capture_output=True,
                                             text=True, timeout=120, check=False)
                    code, log = process.returncode, process.stdout + process.stderr
                except subprocess.TimeoutExpired as exc:
                    code, log = 124, str(exc)
                (args.output / f'{name}.log').write_text(log)
                label = 'BGFX-Vulkan' if backend == 'bgfx' else 'WebGPU'
                timing = re.search(rf'^{label}_timing_total frames=60 median_ms=([\d.e+-]+) '
                                   r'p95_ms=([\d.e+-]+)', log, re.M)
                adapter = re.search(r'^adapter=.+$', log, re.M)
                rss = re.search(r'MAX_RSS_KIB=(\d+)', log)
                expected = '1002' if gpu == 'amd' else '10de'
                if code or not timing or not adapter or not rss or f'vendor_id=0x{expected}' not in adapter.group(0):
                    raise RuntimeError(f'{name}: exit={code}, log={log[-1000:]}')
                row = dict(cell=name, gpu=gpu, backend=backend, factor=factor,
                           repetition=repetition, median_ms=float(timing.group(1)),
                           p95_ms=float(timing.group(2)), max_rss_kib=int(rss.group(1)),
                           gpu_receipt=adapter.group(0))
                rows.append(row)
                (args.output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
                print(f'{name}: median={row["median_ms"]:.3f}ms '
                      f'p95={row["p95_ms"]:.3f}ms rss={row["max_rss_kib"]}KiB', flush=True)

for gpu in ('amd', 'nvidia'):
    for backend in ('bgfx', 'wgpu'):
        low = statistics.median(row['median_ms'] for row in rows if row['gpu'] == gpu and
                                row['backend'] == backend and row['factor'] == 1)
        high = statistics.median(row['median_ms'] for row in rows if row['gpu'] == gpu and
                                 row['backend'] == backend and row['factor'] == 3)
        print(f'{gpu}-{backend}: median-of-medians {low:.3f}ms -> {high:.3f}ms '
              f'({high / low:.2f}x)', flush=True)
