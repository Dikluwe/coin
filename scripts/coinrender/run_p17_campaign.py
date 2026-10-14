#!/usr/bin/env python3
"""Run the P17 physical-adapter window/offscreen matrix in one isolated X11 session."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time


BACKENDS = ('coin-gl', 'bgfx-opengl', 'bgfx-vulkan', 'wgpu-vulkan')
MODES = {'coin-gl': ('object', 'sorted_layers'),
         'bgfx-opengl': ('object', 'weighted_oit', 'sorted_layers'),
         'bgfx-vulkan': ('object', 'weighted_oit', 'sorted_layers'),
         'wgpu-vulkan': ('object', 'sorted_layers')}
OFFSCREEN_LABELS = {'coin-gl': 'CoinGL', 'bgfx-opengl': 'BGFX-OpenGL',
                    'bgfx-vulkan': 'BGFX-Vulkan', 'wgpu-vulkan': 'WebGPU'}


def fields(line):
    return dict(token.split('=', 1) for token in shlex.split(line)
                if '=' in token)


def positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise ValueError('invalid positive metric: ' + value)
    return number


def parse_result(output, target, backend, mode, scene, update, side, warmup, frames):
    lines = output.splitlines()
    if 'Rendering using SORTED_OBJECTS_BLEND instead' in output:
        raise ValueError('sorted layer mode fell back to object blend')
    if target == 'window':
        reports = [fields(line) for line in lines if line.startswith('window_benchmark ')]
        if len(reports) != 1:
            raise ValueError('expected exactly one window report')
        report = reports[0]
        expected = {'backend': backend, 'transparency': mode, 'scene': str(scene),
                    'size': f'{side}x{side}', 'warmup': str(warmup),
                    'frames': str(frames), 'scene_update': update,
                    'readback': 'none', 'present_policy': 'off-requested'}
        for key, value in expected.items():
            if report.get(key) != value:
                raise ValueError(f'{key}: expected {value}, got {report.get(key)}')
        values = {key: positive(report[key]) for key in
                  ('cpu_frame_median_ms', 'cpu_frame_p95_ms', 'total_ms', 'throughput_fps')}
        adapter = report.get('adapter', '')
        ids = (report.get('vendor_id'), report.get('device_id'))
    else:
        headers = [line for line in lines if line.startswith('adapter=')]
        if len(headers) != 1:
            raise ValueError('expected exactly one offscreen header')
        header = headers[0]
        label = OFFSCREEN_LABELS[backend]
        match = re.fullmatch(r'adapter=(.+?) vendor_id=(0x[0-9a-fA-F]+) device_id=(0x[0-9a-fA-F]+) (backend=.*)', header)
        if not match:
            raise ValueError('malformed offscreen adapter header')
        adapter, vendor, device = match.group(1, 2, 3)
        report = fields(match.group(4))
        expected = {'backend': 'gl' if backend == 'coin-gl' else
                    'wgpu' if backend == 'wgpu-vulkan' else 'bgfx',
                    'transparency': mode, 'scene': str(scene), 'size': f'{side}x{side}',
                    'warmup': str(warmup), 'scene_update': update,
                    'mode': 'render+rgba-readback', 'rgba_output': 'copy',
                    'pipeline_depth': '1' if backend.startswith('bgfx-') else '0'}
        for key, value in expected.items():
            if report.get(key) != value:
                raise ValueError(f'{key}: expected {value}, got {report.get(key)}')
        timings = [fields(line) for line in lines if line.startswith(label + ' ')]
        throughputs = [fields(line) for line in lines if line.startswith(label + '_throughput ')]
        if len(timings) != 1 or len(throughputs) != 1:
            raise ValueError('expected one timing and throughput report')
        timing, throughput = timings[0], throughputs[0]
        if timing.get('frames') != str(frames) or throughput.get('frames') != str(frames):
            raise ValueError('incorrect measured frame count')
        values = {'cpu_frame_median_ms': positive(timing['median_ms']),
                  'cpu_frame_p95_ms': positive(timing['p95_ms']),
                  'total_ms': positive(throughput['total_ms']),
                  'throughput_fps': positive(throughput['fps'])}
        ids = (vendor, device)
    if not adapter or adapter == 'not-queried' and backend != 'coin-gl':
        raise ValueError('missing adapter')
    if backend in ('bgfx-vulkan', 'wgpu-vulkan') and ids != ('0x1002', '0x1638'):
        raise ValueError(f'physical Vulkan adapter changed: {ids}')
    if values['cpu_frame_p95_ms'] < values['cpu_frame_median_ms']:
        raise ValueError('p95 below median')
    return dict(adapter=adapter, vendor_id=ids[0], device_id=ids[1], **values)


def capture(command, **kwargs):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        raise RuntimeError(f'{command[0]} exited {result.returncode}: {result.stdout[-1500:]}')
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--bgfx-build', type=Path, required=True)
    parser.add_argument('--wgpu-build', type=Path, required=True)
    parser.add_argument('--scenes-dir', type=Path, required=True)
    parser.add_argument('--size', type=int, default=512)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--frames', type=int, default=180)
    parser.add_argument('--repetitions', type=int, default=2)
    args = parser.parse_args()
    if not os.getenv('COIN_TEST_ISOLATED_X11'):
        parser.error('P17 window campaign requires the isolated X11 session')
    if min(args.size, args.frames, args.repetitions) < 1 or args.warmup < 0:
        parser.error('invalid campaign parameters')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((args.scenes_dir/'scenes.json').read_text())
    scenes = {}
    for name in ('opaque-interleaved', 'transparent-overlap'):
        item = manifest[name]
        path = args.scenes_dir/(name+'.iv')
        if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError('scene hash changed: ' + str(path))
        scenes[name] = {'path': str(path.resolve()), 'sha256': item['sha256'],
                        'objects': item['objects']}
    glx = capture(['glxinfo', '-B'])
    (args.output_dir/'glxinfo-B.log').write_text(glx+'\n')
    renderer = next((line for line in glx.splitlines()
                     if line.startswith('OpenGL renderer string: ')), '')
    if ('Accelerated: yes' not in glx or 'radeonsi' not in renderer.lower()
            or 'zink' in renderer.lower()):
        raise RuntimeError('expected accelerated Radeon radeonsi GLX renderer')
    if os.getenv('VK_DRIVER_FILES') != '/usr/share/vulkan/icd.d/radeon_icd.json':
        raise RuntimeError('pin VK_DRIVER_FILES to the physical Radeon ICD')
    if (os.getenv('__GLX_VENDOR_LIBRARY_NAME') != 'mesa' or
            os.getenv('__EGL_VENDOR_LIBRARY_FILENAMES') !=
            '/usr/share/glvnd/egl_vendor.d/50_mesa.json'):
        raise RuntimeError('pin Mesa GLX/EGL vendors for the Radeon adapter')
    builds = {}
    for name, path in (('bgfx', args.bgfx_build), ('wgpu', args.wgpu_build)):
        cache = (path/'CMakeCache.txt').read_text()
        if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
            raise RuntimeError(name + ' build is not Release')
        executable = path/'bin/coin_render_window_benchmark'
        if not executable.is_file():
            raise RuntimeError('missing ' + str(executable))
        builds[name] = str(path.resolve())
    metadata = {'git_commit': capture(['git', 'rev-parse', 'HEAD']),
                'git_status': capture(['git', 'status', '--short']),
                'hostname': capture(['hostname']), 'uname': capture(['uname', '-srmo']),
                'date_utc': capture(['date', '-u', '+%Y-%m-%dT%H:%M:%SZ']),
                'display': os.getenv('DISPLAY'), 'glxinfo_B': glx,
                'vulkan_icd': os.environ['VK_DRIVER_FILES'],
                'builds': builds, 'scenes': scenes,
                'size': [args.size, args.size], 'warmup': args.warmup,
                'frames': args.frames, 'repetitions': args.repetitions,
                'readback': {'window': 'none', 'offscreen': 'RGBA copy, synchronous'},
                'present_policy': 'no-vsync requested; GLX/BGFX compositor acceptance unverified; wgpu Immediate/Mailbox required',
                'unsupported': ['Coin/GL weighted_oit', 'wgpu weighted_oit on translucent geometry']}
    results = {'metadata': metadata, 'runs': [], 'failures': []}
    result_path = args.output_dir/'results.json'
    result_path.write_text(json.dumps(results, indent=2)+'\n')
    total = sum(len(MODES[b]) if scene == 'transparent-overlap' else 1
                for scene in scenes for b in BACKENDS) * 2 * 2 * args.repetitions
    count = 0
    for target in ('window', 'offscreen'):
        for scene_name, scene in scenes.items():
            for update in ('static', 'dynamic'):
                for backend in BACKENDS:
                    for mode in (MODES[backend] if scene_name == 'transparent-overlap' else ('object',)):
                        for repetition in range(1, args.repetitions+1):
                            count += 1
                            key = f'{target}-{scene_name}-{update}-{backend}-{mode}-r{repetition}'
                            path = Path(builds['wgpu' if backend == 'wgpu-vulkan' else 'bgfx'])
                            executable = path/'bin'/('coin_render_window_benchmark' if target == 'window' else 'coin_render_gl_benchmark')
                            cmd = [str(executable), '--backend', backend if target == 'window' else
                                   'gl' if backend == 'coin-gl' else 'wgpu' if backend == 'wgpu-vulkan' else 'bgfx',
                                   '--transparency', mode, '--scene', scene['path'],
                                   '--warmup', str(args.warmup), '--frames', str(args.frames)]
                            if target == 'window':
                                cmd += ['--width', str(args.size), '--height', str(args.size)]
                            else:
                                cmd += ['--size', str(args.size)]
                            if update == 'dynamic':
                                cmd.append('--dynamic')
                            env = dict(os.environ, LD_LIBRARY_PATH=str(path/'lib'),
                                       COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
                            env.pop('COIN_RENDER_TRACE_PHASES', None)
                            env.pop('COIN_WGPU_GPU_TIMESTAMPS', None)
                            env.pop('COIN_BGFX_DISABLE_DRAW_GROUPING', None)
                            env.pop('COIN_RENDER_BENCH_NO_VSYNC', None)
                            if backend.startswith('bgfx-'):
                                env['COIN_BGFX_RENDERER'] = backend.split('-', 1)[1]
                            else:
                                env.pop('COIN_BGFX_RENDERER', None)
                            start = time.monotonic()
                            try:
                                process = subprocess.run(cmd, env=env, text=True,
                                                         stdout=subprocess.PIPE,
                                                         stderr=subprocess.STDOUT, timeout=120)
                                output = process.stdout
                                (args.output_dir/(key+'.log')).write_text(output)
                                if process.returncode:
                                    raise RuntimeError(f'exit {process.returncode}: {output[-1200:]}')
                                parsed = parse_result(output, target, backend, mode, scene['path'],
                                                      'static' if update == 'static' else
                                                      'transform-each-frame' if target == 'window' else
                                                      'camera-each-frame', args.size, args.warmup, args.frames)
                                results['runs'].append(dict(key=key, target=target, scene=scene_name,
                                                           update=update, backend=backend, mode=mode,
                                                           repetition=repetition, wall_seconds=time.monotonic()-start,
                                                           log=key+'.log', command=cmd, **parsed))
                                status = 'PASS'
                            except Exception as exc:
                                results['failures'].append(dict(key=key, error=str(exc)))
                                status = 'FAIL ' + str(exc)[:120]
                            result_path.write_text(json.dumps(results, indent=2)+'\n')
                            print(f'[{count}/{total}] {key}: {status}', flush=True)
    print(f'P17 runs={len(results["runs"])} failures={len(results["failures"])} artifacts={args.output_dir}', flush=True)
    return 1 if results['failures'] or len(results['runs']) != total else 0


if __name__ == '__main__':
    sys.exit(main())
