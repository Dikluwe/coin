#!/usr/bin/env python3
"""Validate explicit window RGBA capture and the no-readback default."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def run(command, env):
    result = subprocess.run(command, env=env, text=True, capture_output=True,
                            timeout=90, check=False)
    if result.returncode:
        raise RuntimeError(f'{command[0]} exited {result.returncode}: {result.stderr[-1500:]}')
    return result.stdout, result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bgfx-build', type=Path, required=True)
    parser.add_argument('--wgpu-build', type=Path, required=True)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if not os.getenv('COIN_TEST_ISOLATED_X11'):
        parser.error('run through testsuite/qt-quarter/run_isolated.py')
    if os.getenv('VK_DRIVER_FILES') != '/usr/share/vulkan/icd.d/radeon_icd.json':
        parser.error('pin RADV on the physical Radeon')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    records = []
    for backend, build in (('bgfx-opengl', args.bgfx_build),
                           ('bgfx-vulkan', args.bgfx_build),
                           ('wgpu-vulkan', args.wgpu_build)):
        for capture in (False, True):
            env = os.environ.copy()
            env['LD_LIBRARY_PATH'] = str((build / 'lib').resolve()) + ':' + env.get('LD_LIBRARY_PATH', '')
            env['COIN_BGFX_RENDERER'] = 'opengl' if backend == 'bgfx-opengl' else 'vulkan'
            env['COIN_RENDER_TRACE_PHASES'] = '1'
            command = [str((build / 'bin/coin_render_window_benchmark').resolve()),
                       '--backend', backend, '--scene', str(args.scene.resolve()),
                       '--width', '128', '--height', '128', '--warmup', '1', '--frames', '3']
            if capture: command.append('--capture-window')
            stdout, stderr = run(command, env)
            name = backend + ('-capture' if capture else '-normal')
            (args.output_dir / (name + '.out')).write_text(stdout)
            (args.output_dir / (name + '.err')).write_text(stderr)
            if ('readback=rgba-on-request' if capture else 'readback=none') not in stdout:
                raise RuntimeError(name + ': wrong readback mode')
            hashes = re.findall(r'^window_rgba_fnv64=(0x[0-9a-f]+)$', stdout, re.M)
            if capture and len(hashes) != 1 or not capture and hashes:
                raise RuntimeError(name + ': unexpected RGBA checksum')
            if backend.startswith('bgfx-'):
                lines = [line for line in stderr.splitlines()
                         if line.startswith('COIN_RENDER_PHASE bgfx ')]
                expected = 'readback_published_bytes=65536' if capture else 'readback_published_bytes=0'
                if len(lines) != 4 or any(field not in line for line in lines for field in
                    ('readback_pipeline_bytes=0', 'readback_gpu_staging_bytes=0',
                     'readback_cpu_staging_bytes=0', expected)):
                    raise RuntimeError(name + ': BGFX capture telemetry mismatch')
            else:
                lines = [line for line in stderr.splitlines()
                         if line.startswith('COIN_RENDER_PHASE rust_resources target=window ')]
                expected = 'staging_frame_color_bytes=65536' if capture else 'staging_frame_color_bytes=0'
                if len(lines) != 4 or any(field not in line for line in lines for field in
                    (expected, 'staging_frame_depth_bytes=0', 'staging_pool_free_bytes=0')):
                    raise RuntimeError(name + ': wgpu capture telemetry mismatch')
            records.append({'backend': backend, 'mode': 'capture' if capture else 'normal',
                            'rgba_fnv64': hashes[0] if hashes else None})
            print(name, hashes[0] if hashes else 'no readback', flush=True)
    if len({row['rgba_fnv64'] for row in records if row['mode'] == 'capture'}) != 1:
        raise RuntimeError('BGFX OpenGL/Vulkan and wgpu window RGBA differs')
    (args.output_dir / 'results.json').write_text(json.dumps(records, indent=2) + '\n')

if __name__ == '__main__':
    main()
