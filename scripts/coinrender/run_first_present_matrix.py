#!/usr/bin/env python3
"""Observe single submissions on physical Linux GPUs; never replace an early capture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wgpu-build', type=Path, required=True)
    parser.add_argument('--bgfx-build', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--gl-repetitions', type=int, default=20)
    parser.add_argument('--terrain-matrix', action='store_true', help='Use the historical illuminated terrain through CoinRenderAction')
    args = parser.parse_args()
    if not 1 <= args.gl_repetitions <= 100:
        parser.error('gl-repetitions must be 1..100')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for backend, build in [('wgpu', args.wgpu_build), ('bgfx', args.bgfx_build)]:
        for gpu in ['amd', 'nvidia']:
            for api in ['vulkan', 'opengl']:
                env = dict(os.environ, DISPLAY=os.getenv('DISPLAY', ':0'),
                           VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('radeon_icd.json' if gpu == 'amd' else 'nvidia_icd.json'),
                           __GLX_VENDOR_LIBRARY_NAME='mesa' if gpu == 'amd' else 'nvidia',
                           __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('50_mesa.json' if gpu == 'amd' else '10_nvidia.json'),
                           WGPU_BACKEND='vulkan' if api == 'vulkan' else 'gl',
                           COIN_BGFX_RENDERER=api, COIN_RENDER_TRACE_PHASES='1',
                           COIN_BGFX_TRACE_GL_ADAPTER='1')
                env['VK_ICD_FILENAMES'] = env['VK_DRIVER_FILES']
                env.pop('COIN_RENDER_BENCH_NO_VSYNC', None)
                if gpu == 'nvidia':
                    env['__NV_PRIME_RENDER_OFFLOAD'] = '1'
                else:
                    env.pop('__NV_PRIME_RENDER_OFFLOAD', None)
                variants = [('million-native', []), ('million-canonical', ['--canonical']),
                            ('million-managed', ['--managed']), ('small-native', ['--small'])]
                if backend == 'wgpu':
                    variants += [('overdraw-native', ['--overdraw'])]
                if args.terrain_matrix:
                    variants = [('terrain-native-managed', ['--terrain', '--managed']),
                                ('terrain-canonical-managed', ['--terrain', '--managed', '--canonical']),
                                ('terrain-double-managed', ['--terrain', '--managed', '--legacy-double-submit'])]
                if backend == 'wgpu' and gpu == 'amd' and api == 'opengl':
                    flags = ['--terrain', '--managed'] if args.terrain_matrix else ['--managed']
                    variants += [('repeat-managed-%02d' % i, flags) for i in range(args.gl_repetitions)]
                    negative_flags = ['--terrain', '--managed'] if args.terrain_matrix else ['--small']
                    variants += [('negative-stale', negative_flags+['--suppress-second-submit'])]
                if backend == 'wgpu' and gpu == 'nvidia' and api == 'opengl':
                    variants = [('availability', ['--small'])]
                for name, flags in variants:
                    stem = '-'.join([backend, gpu, api, name])
                    command = [str(build.resolve()/'bin/CoinRenderFirstPresentTest')] + flags
                    proc = subprocess.run(command, env=env, text=True, capture_output=True, timeout=180)
                    log = proc.stdout + proc.stderr
                    (args.output_dir/(stem+'.log')).write_text(log)
                    api_ok = (('renderer='+api+' ') in log if backend == 'wgpu' else
                              ('bgfx_device renderer='+('Vulkan' if api == 'vulkan' else 'OpenGL')) in log)
                    if api == 'vulkan':
                        physical = ('vendor_id=0x1002 device_id=0x1638' if gpu == 'amd' else 'vendor_id=0x10de device_id=0x2560') in log
                    elif backend == 'wgpu':
                        physical = 'vendor_id=0x1002' in log and 'renoir' in log and 'AMD Radeon Graphics' in log
                    else:
                        physical = ('bgfx_gl_adapter vendor=AMD renderer=AMD Radeon Graphics' if gpu == 'amd' else
                                    'bgfx_gl_adapter vendor=NVIDIA Corporation renderer=NVIDIA GeForce RTX 3060') in log
                    observations = [dict(zip(['frame', 'triangles', 'width', 'height', 'immediate_fresh', 'fixed_50ms_fresh',
                                             'eventual_fresh', 'elapsed_ms', 'polls', 'serial', 'serial_unchanged'], match))
                                    for match in re.findall(r'first_present frame=(\d+) triangles=(\d+) extent=(\d+)x(\d+) immediate_fresh=(\d+) fixed_50ms_fresh=(\d+) eventual_fresh=(\d+) elapsed_ms=(\S+) polls=(\d+) serial=(\d+) serial_unchanged=(\d+) no_new_render=1', log)]
                    if name == 'availability':
                        passed = proc.returncode == 77
                    elif name == 'negative-stale':
                        passed = proc.returncode == 1 and len(observations) == 2 and observations[-1]['eventual_fresh'] == '0' and 'B03 single-submit observer FAIL' in log
                    else:
                        passed = proc.returncode == 0 and len(observations) == 3 and all(x['eventual_fresh'] == '1' and x['serial_unchanged'] == '1' for x in observations) and log.count('mismatches=0 max_rgb=0') == 3
                        if args.terrain_matrix:
                            passed = passed and log.count('reference_after_capture=1 result=PASS') == 3
                    row = {'backend': backend, 'gpu': gpu, 'api': api, 'variant': name, 'command': command,
                           'returncode': proc.returncode, 'api_verified': api_ok, 'physical_gpu_verified': physical,
                           'passed': passed and (name == 'availability' or (api_ok and physical)),
                           'availability_only': name == 'availability', 'expected_failure': name == 'negative-stale',
                           'terrain_matrix': args.terrain_matrix,
                           'observations': observations, 'log': stem+'.log', 'executable_sha256': sha(Path(command[0])),
                           'library_sha256': {n: sha(build/'lib'/n) for n in ['libCoinRender.so','libCoin.so']},
                           'environment': {k: v for k, v in env.items() if k in ['DISPLAY', 'VK_DRIVER_FILES', 'VK_ICD_FILENAMES', '__GLX_VENDOR_LIBRARY_NAME', '__EGL_VENDOR_LIBRARY_FILENAMES', '__NV_PRIME_RENDER_OFFLOAD', 'WGPU_BACKEND', 'COIN_BGFX_RENDERER', 'COIN_RENDER_TRACE_PHASES', 'COIN_BGFX_TRACE_GL_ADAPTER']}}
                    rows.append(row)
                    (args.output_dir/'results.json').write_text(json.dumps(rows, indent=2)+'\n')
                    print(stem, 'PASS' if row['passed'] else 'FAIL', 'rc='+str(proc.returncode), flush=True)
    failures = [x['log'] for x in rows if not x['passed']]
    summary = {'terrain_matrix': args.terrain_matrix, 'processes': len(rows), 'positive_processes': sum(not x['expected_failure'] and not x['availability_only'] for x in rows),
               'negative_controls': sum(x['expected_failure'] for x in rows), 'unavailable': sum(x['availability_only'] for x in rows),
               'immediate_stale_observations': sum(y['immediate_fresh'] == '0' for x in rows if not x['expected_failure'] for y in x['observations']),
               'fixed_50ms_stale_observations': sum(y['fixed_50ms_fresh'] == '0' for x in rows if not x['expected_failure'] for y in x['observations']),
               'failures': failures, 'historical_50ms_failure_cause_proved': False}
    (args.output_dir/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
