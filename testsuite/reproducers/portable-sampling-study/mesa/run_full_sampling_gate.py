#!/usr/bin/env python3
"""Run the strict BGFX sampling gate with the private Mesa reference on Linux."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


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
    lib = mesa / 'lib/x86_64-linux-gnu'
    amd_icd = mesa / 'share/vulkan/icd.d/radeon_icd.x86_64.json'
    mesa_egl = mesa / 'share/glvnd/egl_vendor.d/50_mesa.json'
    required = [binary, lib / 'libgallium-25.2.8.so',
                lib / 'dri/radeonsi_dri.so', amd_icd, mesa_egl]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        parser.error('missing private runtime/build files: ' + ', '.join(missing))
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for gpu in ('amd', 'nvidia'):
        for api in ('vulkan', 'opengl'):
            for policy in ('native', 'portable'):
                env = dict(os.environ)
                for key in ('__NV_PRIME_RENDER_OFFLOAD', 'EGL_PLATFORM',
                            'COIN_SAMPLING_STUDY', 'AMD_DEBUG',
                            'AMD_FORCE_SHADER_USE_ACO', 'COIN_RENDER_TRACE_PHASES'):
                    env.pop(key, None)
                icd = (str(amd_icd) if gpu == 'amd' else
                       '/usr/share/vulkan/icd.d/nvidia_icd.json')
                env.update(DISPLAY=args.display, COIN_BGFX_RENDERER=api,
                           COIN_GLX_PIXMAP_DIRECT_RENDERING='1',
                           COIN_GLXGLUE_NO_PBUFFERS='1',
                           COIN_RENDER_REQUIRE_GL_REFERENCE='1',
                           COIN_MESA_MIXED_FILTER_STUDY='1',
                           MESA_SHADER_CACHE_DISABLE='true',
                           NIR_DEBUG='validate',
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
                command = [str(binary), '--gpu']
                if policy == 'portable':
                    command.append('--portable-sampling')
                log = output / f'{gpu}-{api}-{policy}.log'
                try:
                    with log.open('w') as stream:
                        code = subprocess.run(command, env=env, cwd=output,
                                              stdout=stream,
                                              stderr=subprocess.STDOUT,
                                              timeout=240).returncode
                except subprocess.TimeoutExpired:
                    code = 124
                lines = log.read_text(errors='replace').splitlines()
                adapter = next((line for line in lines if line.startswith('adapter=')), '')
                gl_adapter = next((line for line in lines if 'bgfx_gl_adapter ' in line), '')
                projective = [line for line in lines if line.startswith(
                    'projective mip footprint/quality-0.500000/offset-0.000000/')
                    and 'samples=' in line]
                finish = next((line for line in lines if line.startswith(
                    'P07 sampling cases=')), '')
                expected_gpu = 'AMD' if gpu == 'amd' else 'NVIDIA'
                identity = expected_gpu in (gl_adapter if api == 'opengl' else adapter)
                complete = finish == 'P07 sampling cases=234 rejected=4 GPU=1'
                projective_ok = len(projective) == 3 and all(
                    re.search(r'mae=0 max=0$', line) for line in projective)
                passed = code == 0 and identity and complete and projective_ok
                row = dict(gpu=gpu, api=api, policy=policy, exit=code,
                           pass_gate=passed, adapter=adapter, gl_adapter=gl_adapter,
                           projective=projective, finish=finish, log=log.name,
                           log_sha256=digest(log))
                rows.append(row)
                (output / 'summary.json').write_text(json.dumps(
                    dict(binary=str(binary), binary_sha256=digest(binary),
                         mesa_prefix=str(mesa),
                         mesa_gallium_sha256=digest(lib / 'libgallium-25.2.8.so'),
                         results=rows), indent=2) + '\n')
                print(gpu, api, policy, 'PASS' if passed else 'FAIL',
                      f'exit={code}', adapter, flush=True)
                if not passed:
                    return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
