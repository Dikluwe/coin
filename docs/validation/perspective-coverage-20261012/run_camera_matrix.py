#!/usr/bin/env python3
"""Run the strict CoinGL camera gate at 1x and 3x on the two local GPUs."""

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

results = []
for gpu in ('amd', 'nvidia'):
    for backend in ('bgfx', 'wgpu'):
        for api in ('vulkan', 'opengl'):
            for factor in (1, 3):
                build = args.bgfx_build if backend == 'bgfx' else args.wgpu_build
                name = f'{gpu}-{backend}-{api}-{factor}x'
                env = os.environ.copy()
                for key in tuple(env):
                    if key.startswith('COIN_RENDER_CAMERA_REFERENCE_') or key in (
                        'COIN_RENDER_REQUIRE_CAMERA_REFERENCE', 'COIN_RENDER_REQUIRE_GL_REFERENCE',
                        'COIN_BGFX_RENDERER', 'WGPU_BACKEND', '__NV_PRIME_RENDER_OFFLOAD'):
                        env.pop(key)
                icd = 'radeon' if gpu == 'amd' else 'nvidia'
                env.update(
                    DISPLAY=':0',
                    LD_LIBRARY_PATH=str(build / 'lib'),
                    VK_DRIVER_FILES=f'/usr/share/vulkan/icd.d/{icd}_icd.json',
                    VK_ICD_FILENAMES=f'/usr/share/vulkan/icd.d/{icd}_icd.json',
                    __GLX_VENDOR_LIBRARY_NAME='mesa' if gpu == 'amd' else 'nvidia',
                    __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/' +
                        ('50_mesa.json' if gpu == 'amd' else '10_nvidia.json'),
                    COIN_GLX_PIXMAP_DIRECT_RENDERING='1',
                    COIN_GLXGLUE_NO_PBUFFERS='1' if gpu == 'amd' else '0',
                    COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1',
                    COIN_RENDER_REQUIRE_GL_REFERENCE='1',
                    COIN_RENDER_CAMERA_REFERENCE_EXPECT_GL_RENDERER='AMD' if gpu == 'amd' else 'NVIDIA',
                    COIN_RENDER_CAMERA_REFERENCE_SUPERSAMPLE=str(factor),
                )
                if backend == 'bgfx' and api == 'opengl':
                    # The BGFX capability probe can report vendor 0 for GL;
                    # qualify its live EGL context from the runtime trace.
                    env['COIN_BGFX_TRACE_GL_ADAPTER'] = '1'
                else:
                    env['COIN_RENDER_CAMERA_REFERENCE_EXPECT_VENDOR'] = (
                        '0x1002' if gpu == 'amd' else '0x10de')
                if gpu == 'nvidia':
                    env['__NV_PRIME_RENDER_OFFLOAD'] = '1'
                env['COIN_BGFX_RENDERER' if backend == 'bgfx' else 'WGPU_BACKEND'] = (
                    api if backend == 'bgfx' else 'gl' if api == 'opengl' else 'vulkan')
                cmd = [str(build / 'bin/CoinRenderCameraReuseReferenceTest')]
                try:
                    proc = subprocess.run(cmd, env=env, capture_output=True, text=True,
                                          timeout=120, check=False)
                    code = proc.returncode
                    output = proc.stdout + proc.stderr
                except subprocess.TimeoutExpired as exc:
                    code = 124
                    output = str(exc)
                (args.output / f'{name}.log').write_text(output)
                comparisons = re.findall(r'^(perspective|ortho)/CoinGL frame=(\d+) '
                                         r'rgb_mae=([0-9.]+) max=(\d+) pixels_over3=(\d+)$',
                                         output, re.M)
                gpu_trace = re.findall(r'^COIN_RENDER_PHASE bgfx_gl_adapter .+$', output, re.M)
                qualified_gl = backend != 'bgfx' or api != 'opengl' or any(
                    ('renderer=AMD Radeon' if gpu == 'amd' else 'renderer=NVIDIA') in trace
                    for trace in gpu_trace)
                row = dict(cell=name, exit=code,
                           pass_gate=code == 0 and len(comparisons) == 18 and qualified_gl,
                           comparisons=len(comparisons),
                           gpu_receipts=re.findall(r'^camera_gpu .+$', output, re.M),
                           oracle_receipts=re.findall(r'^camera_oracle .+$', output, re.M),
                           gpu_trace=gpu_trace[:1],
                           max_mae=max((float(x[2]) for x in comparisons), default=None),
                           max_pixels_over3=max((int(x[4]) for x in comparisons), default=None),
                           log=f'{name}.log')
                results.append(row)
                (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
                print(f'{name}: exit={code} comparisons={len(comparisons)} '
                      f'pass={row["pass_gate"]}', flush=True)
