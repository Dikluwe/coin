#!/usr/bin/env python3
"""Compare the expanded legacy POT fallback with CoinGL on Linux GPUs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--mesa-prefix', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--display', default=os.environ.get('DISPLAY', ':0'))
    parser.add_argument('--xauthority', default=os.environ.get('XAUTHORITY', ''))
    args = parser.parse_args()
    build = args.build.resolve()
    mesa = args.mesa_prefix.resolve()
    output = args.output.resolve()
    binary = build / 'bin/CoinRenderTextureSamplingTest'
    helper_source = Path(__file__).resolve().parents[3] / 'coinrender/CoinRenderHideSimageForPotProbe.c'
    scratch = tempfile.TemporaryDirectory(prefix='coin-glu-pot-')
    helper = Path(scratch.name) / 'coin-hide-simage.so'
    lib = mesa / 'lib/x86_64-linux-gnu'
    amd_icd = mesa / 'share/vulkan/icd.d/radeon_icd.x86_64.json'
    mesa_egl = mesa / 'share/glvnd/egl_vendor.d/50_mesa.json'
    required = [binary, helper_source, lib / 'libgallium-25.2.8.so',
                lib / 'dri/radeonsi_dri.so', amd_icd, mesa_egl]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        parser.error('missing runtime/build files: ' + ', '.join(missing))
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run(['cc', '-shared', '-fPIC', '-o', str(helper),
                    str(helper_source), '-ldl'], check=True)
    rows = []
    for gpu in ('amd', 'nvidia'):
        for api in ('vulkan', 'opengl'):
            env = dict(os.environ)
            for key in ('__NV_PRIME_RENDER_OFFLOAD', 'EGL_PLATFORM',
                        'COIN_SAMPLING_STUDY', 'COIN_TEST_HIDE_GLU',
                        'AMD_DEBUG'):
                env.pop(key, None)
            icd = (str(amd_icd) if gpu == 'amd' else
                   '/usr/share/vulkan/icd.d/nvidia_icd.json')
            env.update(DISPLAY=args.display, COIN_BGFX_RENDERER=api,
                       COIN_GLX_PIXMAP_DIRECT_RENDERING='1',
                       COIN_GLXGLUE_NO_PBUFFERS='1',
                       COIN_RENDER_REQUIRE_GL_REFERENCE='1',
                       COIN_GLGLUE_DISABLE_NON_POWER_OF_TWO_TEXTURES='1',
                       COIN_MESA_MIXED_FILTER_STUDY='1',
                       MESA_SHADER_CACHE_DISABLE='true',
                       LD_PRELOAD=str(helper),
                       LD_LIBRARY_PATH=f'{build / "lib"}:{lib}',
                       LIBGL_DRIVERS_PATH=str(lib / 'dri'),
                       __GLX_VENDOR_LIBRARY_NAME='mesa',
                       __EGL_VENDOR_LIBRARY_FILENAMES=str(mesa_egl),
                       VK_DRIVER_FILES=icd, VK_ICD_FILENAMES=icd)
            if args.xauthority:
                env['XAUTHORITY'] = args.xauthority
            if gpu == 'nvidia':
                env['__NV_PRIME_RENDER_OFFLOAD'] = '1'
            if gpu == 'nvidia' and api == 'opengl':
                env['EGL_PLATFORM'] = 'surfaceless'
                env['__EGL_VENDOR_LIBRARY_FILENAMES'] = (
                    '/usr/share/glvnd/egl_vendor.d/10_nvidia.json')
            if api == 'opengl':
                env['COIN_BGFX_TRACE_GL_ADAPTER'] = '1'
            else:
                env.pop('COIN_BGFX_TRACE_GL_ADAPTER', None)
            log = output / f'{gpu}-{api}.log'
            try:
                with log.open('w') as stream:
                    code = subprocess.run(
                        [str(binary), '--scale-policy-pot-glu-probe'],
                        env=env, cwd=output, stdout=stream,
                        stderr=subprocess.STDOUT, timeout=120).returncode
            except subprocess.TimeoutExpired:
                code = 124
            lines = log.read_text(errors='replace').splitlines()
            adapter = next((line for line in lines if line.startswith('adapter=')), '')
            gl_adapter = next((line for line in lines if 'bgfx_gl_adapter ' in line), '')
            comparisons = [line for line in lines if '/GPU-GL samples=' in line]
            maximums = [int(match.group(1)) for line in comparisons
                        if (match := re.search(r'max=(\d+)$', line))]
            expected = 'AMD' if gpu == 'amd' else 'NVIDIA'
            identity = expected in (gl_adapter if api == 'opengl' else adapter)
            passed = (code == 0 and identity and len(maximums) == 72 and
                      max(maximums) <= 2)
            row = dict(gpu=gpu, api=api, exit=code, pass_gate=passed,
                       adapter=adapter, gl_adapter=gl_adapter,
                       comparisons=len(maximums), maximum=max(maximums, default=None),
                       log=log.name, log_sha256=digest(log))
            rows.append(row)
            (output / 'summary.json').write_text(json.dumps(
                dict(binary=str(binary), binary_sha256=digest(binary),
                     helper_sha256=digest(helper), mesa_prefix=str(mesa),
                     mesa_gallium_sha256=digest(lib / 'libgallium-25.2.8.so'),
                     results=rows), indent=2) + '\n')
            print(gpu, api, 'PASS' if passed else 'FAIL',
                  f'exit={code} comparisons={len(maximums)} '
                  f'max={max(maximums, default=None)}', adapter, flush=True)
            if not passed:
                scratch.cleanup()
                return 1
    scratch.cleanup()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
