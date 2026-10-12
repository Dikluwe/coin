#!/usr/bin/env python3
"""Compare P20 offscreen scenes with CoinGL at 1x and center-sampled 3x."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--bgfx-build', required=True, type=Path)
parser.add_argument('--wgpu-build', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
fixture_dir = Path(__file__).resolve().parents[1] / 'hardware-surfaces-linux-20261007/fixtures'


def pixels(path):
    magic, extent, maximum, data = path.read_bytes().split(b'\n', 3)
    width, height = map(int, extent.split())
    if magic != b'P6' or maximum != b'255' or width != height or len(data) != width * height * 3:
        raise ValueError(f'invalid PPM: {path}')
    return width, data


def compare(reference, actual):
    if len(reference) != len(actual):
        raise ValueError('mismatched image size')
    maximum = 0
    over_three = 0
    total = 0
    for index in range(0, len(reference), 3):
        difference = max(abs(reference[index + c] - actual[index + c]) for c in range(3))
        maximum = max(maximum, difference)
        over_three += difference > 3
        total += sum(abs(reference[index + c] - actual[index + c]) for c in range(3))
    mae = total / len(reference)
    return dict(max_rgb=maximum, mae=mae, pixels_over3=over_three,
                pass_gate=maximum <= 2 and mae <= 1.0)


def execute(gpu, backend, scene, factor):
    build = args.bgfx_build if backend in ('gl', 'bgfx') else args.wgpu_build
    name = f'{gpu}-{backend}-{scene}-{factor}x'
    output = args.output / (name + '.ppm')
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
    cmd = [str(build / 'bin/coin_render_gl_benchmark'), '--backend', backend,
           '--scene', str(fixture_dir / f'{scene}.iv'), '--size', '128',
           '--warmup', '1', '--frames', '2', '--image-output', str(output)]
    if backend != 'gl':
        cmd.extend(('--center-sample-scale', str(factor)))
    try:
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True,
                              timeout=120, check=False)
        code, log = proc.returncode, proc.stdout + proc.stderr
    except subprocess.TimeoutExpired as exc:
        code, log = 124, str(exc)
    (args.output / (name + '.log')).write_text(log)
    adapter = re.findall(r'^adapter=.+$', log, re.M)
    expected = '1002' if gpu == 'amd' else '10de'
    if code != 0 or not output.exists() or (backend != 'gl' and
                                            (not adapter or
                                             f'vendor_id=0x{expected}' not in adapter[-1])):
        raise RuntimeError(f'{name}: exit={code}, adapter={adapter[-1:]}, log={log[-800:]}')
    extent, data = pixels(output)
    if extent != 128:
        raise ValueError(f'{name}: wrong image extent')
    return name, data, adapter[-1:]


rows = []
for gpu in ('amd', 'nvidia'):
    for scene in ('opaque-interleaved', 'transparent-overlap'):
        reference_name, reference, _ = execute(gpu, 'gl', scene, 1)
        captured = {}
        for backend in ('bgfx', 'wgpu'):
            for factor in (1, 3):
                name, actual, adapter = execute(gpu, backend, scene, factor)
                captured[backend, factor] = actual
                row = dict(cell=name, reference=reference_name,
                           gpu_receipt=adapter, **compare(reference, actual))
                rows.append(row)
                (args.output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
                print(f'{name}: max={row["max_rgb"]} mae={row["mae"]:.6f} '
                      f'over3={row["pixels_over3"]} pass={row["pass_gate"]}', flush=True)
        for factor in (1, 3):
            if captured['bgfx', factor] != captured['wgpu', factor]:
                raise AssertionError(f'{gpu}/{scene}/{factor}x BGFX/wgpu differ')
