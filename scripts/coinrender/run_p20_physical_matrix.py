#!/usr/bin/env python3
"""Run the same CoinRender scene/readback cells on one pinned physical GPU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


DEVICES = {
    'amd': {'vendor': '0x1002', 'device': '0x1638', 'gl': 'radeonsi',
            'icd': '/usr/share/vulkan/icd.d/radeon_icd.json'},
    'nvidia': {'vendor': '0x10de', 'device': '0x2560', 'gl': 'NVIDIA GeForce RTX 3060',
               'icd': '/usr/share/vulkan/icd.d/nvidia_icd.json'},
}
BACKENDS = ('coin-gl', 'bgfx-opengl', 'bgfx-vulkan', 'wgpu-vulkan')
SCENES = ('opaque-interleaved', 'transparent-overlap')


def run(command, env=None, timeout=90, log=None):
    result = subprocess.run(command, env=env, text=True, capture_output=True,
                            timeout=timeout, check=False)
    if log is not None:
        log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{command[0]} exit={result.returncode}: '
                           f'{(result.stdout + result.stderr)[-1800:]}')
    return result.stdout + result.stderr


def field(report, name):
    values = dict(part.split('=', 1) for part in shlex.split(report)
                  if '=' in part)
    if name not in values:
        raise ValueError('missing ' + name + ' in ' + report[:300])
    return values[name]


def rgb_metrics(left, right):
    """Read the benchmark's top-down P6 files, without correcting pixel values."""
    def read(path):
        header, size, maximum, pixels = path.read_bytes().split(b'\n', 3)
        if header != b'P6' or size != b'128 128' or maximum != b'255' or len(pixels) != 128*128*3:
            raise ValueError('invalid 128x128 RGB fixture: ' + str(path))
        return pixels
    a, b = read(left), read(right)
    differences = [abs(x-y) for x, y in zip(a, b)]
    return {'mae': sum(differences)/len(differences), 'max': max(differences),
            'pixels_over3': sum(max(differences[i:i+3]) > 3 for i in range(0, len(differences), 3))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', choices=DEVICES, required=True)
    parser.add_argument('--bgfx-build', type=Path, required=True)
    parser.add_argument('--wgpu-build', type=Path, required=True)
    parser.add_argument('--scenes-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--session', choices=('isolated', 'physical'), default='isolated')
    parser.add_argument('--targets', choices=('window', 'offscreen'), nargs='+', default=['window', 'offscreen'])
    args = parser.parse_args()
    if args.session == 'isolated' and not os.getenv('COIN_TEST_ISOLATED_X11'):
        parser.error('run inside testsuite/qt-quarter/run_isolated.py')
    if args.session == 'physical' and not os.getenv('DISPLAY'):
        parser.error('physical session requires an explicit DISPLAY')
    device = DEVICES[args.device]
    if os.getenv('VK_DRIVER_FILES') != device['icd']:
        parser.error('pin the physical device ICD via VK_DRIVER_FILES')
    if args.device == 'nvidia' and (os.getenv('__NV_PRIME_RENDER_OFFLOAD') != '1' or
                                   os.getenv('__GLX_VENDOR_LIBRARY_NAME') != 'nvidia'):
        parser.error('pin NVIDIA PRIME and GLX vendor')
    if args.device == 'amd' and os.getenv('__GLX_VENDOR_LIBRARY_NAME') != 'mesa':
        parser.error('pin Mesa GLX vendor')
    egl_vendor = ('10_nvidia.json' if args.device == 'nvidia' else '50_mesa.json')
    if os.getenv('__EGL_VENDOR_LIBRARY_FILENAMES') != (
            '/usr/share/glvnd/egl_vendor.d/' + egl_vendor):
        parser.error('pin the matching physical EGL vendor')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    glx = run(['glxinfo', '-B'])
    (args.output_dir / 'glxinfo-B.log').write_text(glx)
    renderer = re.search(r'^OpenGL renderer string: (.+)$', glx, re.M)
    if not renderer or device['gl'].lower() not in renderer.group(1).lower() or \
            'direct rendering: Yes' not in glx:
        raise RuntimeError('physical OpenGL renderer mismatch: ' + str(renderer))
    pci = run(['lspci', '-nn'])
    if f"[{device['vendor'][2:]}:{device['device'][2:]}]" not in pci.lower():
        raise RuntimeError('physical PCI device missing')
    (args.output_dir / 'lspci.log').write_text(pci)
    vulkan = run(['vulkaninfo', '--summary'])
    (args.output_dir / 'vulkaninfo-summary.log').write_text(vulkan)
    if not re.search(r'vendorID\s*=\s*' + device['vendor'], vulkan, re.I) or \
            not re.search(r'deviceID\s*=\s*' + device['device'], vulkan, re.I):
        raise RuntimeError('physical Vulkan device missing')
    manifest = json.loads((args.scenes_dir / 'scenes.json').read_text())
    scenes = {}
    for name in SCENES:
        path = (args.scenes_dir / (name + '.iv')).resolve()
        sha = hashlib.sha256(path.read_bytes()).hexdigest()
        if sha != manifest[name]['sha256']:
            raise RuntimeError('fixture checksum changed: ' + name)
        scenes[name] = {'path': str(path), 'sha256': sha}
    builds = {'bgfx': args.bgfx_build.resolve(), 'wgpu': args.wgpu_build.resolve()}
    for build in builds.values():
        if 'CMAKE_BUILD_TYPE:STRING=Release' not in (build / 'CMakeCache.txt').read_text():
            raise RuntimeError('P20 requires Release build: ' + str(build))
    results = {'device': args.device, 'expected_ids': [device['vendor'], device['device']],
               'gl_renderer': renderer.group(1), 'icd': device['icd'],
               'vulkan_driver': re.findall(r'^\s*(?:driverName|driverInfo)\s*=\s*(.+)$', vulkan, re.M),
               'git_commit': run(['git', 'rev-parse', 'HEAD']).strip(),
               'builds': {name: {'path': str(build),
                                 'window_sha256': hashlib.sha256((build / 'bin' /
                                     'coin_render_window_benchmark').read_bytes()).hexdigest(),
                                 'offscreen_sha256': hashlib.sha256((build / 'bin' /
                                     'coin_render_gl_benchmark').read_bytes()).hexdigest()}
                          for name, build in builds.items()},
               'uname': run(['uname', '-srmo']).strip(), 'scenes': scenes, 'size': 128, 'warmup': 1, 'frames': 3,
               'session': args.session, 'targets': args.targets,
               'runs': [], 'failures': [], 'comparisons': []}
    result_file = args.output_dir / 'results.json'
    for target in args.targets:
        for scene_name in SCENES:
            for backend in BACKENDS:
                key = f'{args.device}-{target}-{scene_name}-{backend}'
                build = builds['wgpu' if backend == 'wgpu-vulkan' else 'bgfx']
                executable = build / 'bin' / ('coin_render_window_benchmark' if target == 'window'
                                              else 'coin_render_gl_benchmark')
                mapped = backend if target == 'window' else ('gl' if backend == 'coin-gl' else
                            'wgpu' if backend == 'wgpu-vulkan' else 'bgfx')
                command = [str(executable), '--backend', mapped, '--scene', scenes[scene_name]['path'],
                           '--warmup', '1', '--frames', '3']
                command += (['--width', '128', '--height', '128', '--capture-window']
                            if target == 'window' else ['--size', '128'])
                env = dict(os.environ, LD_LIBRARY_PATH=str(build / 'lib') + ':' +
                           os.environ.get('LD_LIBRARY_PATH', ''),
                           COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
                env.pop('COIN_RENDER_TRACE_PHASES', None)
                env['COIN_RENDER_TRACE_PHASES'] = '1' if backend == 'bgfx-opengl' else '0'
                env['COIN_BGFX_TRACE_GL_ADAPTER'] = '1'
                command += ['--image-output', str(args.output_dir / (key + '.ppm'))]
                env['COIN_BGFX_RENDERER'] = ('opengl' if backend == 'bgfx-opengl' else 'vulkan')
                try:
                    output = run(command, env=env, log=args.output_dir / (key + '.log'))
                    image = (args.output_dir / (key+'.ppm')).read_bytes().split(b'\n',3)[-1]
                    colors = {image[i:i+3] for i in range(0,len(image),3)}
                    if len(image) != 128*128*3 or len(colors) < 3:
                        raise RuntimeError('empty or degenerate RGB capture')
                    prefix = ('window_rgba_fnv64' if target == 'window' else
                              'gl_rgba_fnv64' if backend == 'coin-gl' else 'rgba_fnv64')
                    hashes = re.findall(r'^' + prefix + r'=(0x[0-9a-f]+)$', output, re.M)
                    if len(hashes) != 1:
                        raise RuntimeError('missing or unexpected RGBA checksum')
                    report_prefix = 'window_benchmark ' if target == 'window' else 'adapter='
                    reports = [line for line in output.splitlines()
                               if line.startswith(report_prefix)]
                    if len(reports) != 1:
                        raise RuntimeError('missing unique adapter report')
                    report = reports[0]
                    if backend == 'bgfx-opengl':
                        actual = re.findall(r'bgfx_gl_adapter vendor=(.*?) renderer=(.*?) version=', output)
                        if not actual or device['gl'].lower() not in actual[-1][1].lower():
                            raise RuntimeError('current BGFX EGL context does not prove the requested GPU')
                    if target == 'window':
                        adapter = field(report, 'adapter')
                        vendor, product = field(report, 'vendor_id'), field(report, 'device_id')
                    else:
                        adapter = report.split(' vendor_id=', 1)[0].removeprefix('adapter=')
                        vendor, product = field(report, 'vendor_id'), field(report, 'device_id')
                    if backend in ('bgfx-vulkan', 'wgpu-vulkan') and \
                            (vendor, product) != (device['vendor'], device['device']):
                        raise RuntimeError(f'wrong Vulkan adapter: {vendor}:{product}')
                    if backend == 'coin-gl' and adapter not in ('not-queried', 'native adapter') and \
                            device['gl'].lower() not in adapter.lower():
                        raise RuntimeError('wrong OpenGL adapter: ' + adapter)
                    # BGFX GL Caps may report 0:0. The pinned GLX session is the proof.
                    if target == 'window' and field(report, 'readback') != 'rgba-on-request':
                        raise RuntimeError('unexpected window readback mode')
                    results['runs'].append({'key': key, 'target': target, 'scene': scene_name,
                                            'backend': backend, 'adapter': adapter,
                                            'vendor_id': vendor, 'device_id': product,
                                            'rgba_fnv64': hashes[0] if hashes else None,
                                            'qualification': 'capture; see RGB comparisons',
                                            'log': key + '.log'})
                    print(key, hashes[0] if hashes else 'presentation-only', flush=True)
                except Exception as exc:
                    results['failures'].append({'key': key, 'error': str(exc)})
                    print(key, 'FAIL', str(exc)[:160], flush=True)
                result_file.write_text(json.dumps(results, indent=2) + '\n')
    indexed = {(row['target'], row['scene'], row['backend']): row
               for row in results['runs']}
    for target in args.targets:
        for backend in BACKENDS:
            images = [indexed.get((target, scene, backend)) for scene in SCENES]
            if all(images) and (args.output_dir / (images[0]['key']+'.ppm')).read_bytes() == \
                              (args.output_dir / (images[1]['key']+'.ppm')).read_bytes():
                results['failures'].append({'key': f'{args.device}-{target}-{backend}',
                                           'error': 'distinct fixtures produced identical RGB'})
    for target in args.targets:
        for scene in SCENES:
            left = indexed.get((target, scene, 'bgfx-vulkan'))
            right = indexed.get((target, scene, 'wgpu-vulkan'))
            equal = bool(left and right and left['rgba_fnv64'] == right['rgba_fnv64'])
            results['comparisons'].append({'target': target, 'scene': scene,
                                           'backends': ['bgfx-vulkan', 'wgpu-vulkan'],
                                           'policy': 'exact RGBA8 FNV within one physical GPU',
                                           'equal': equal})
            if not equal:
                results['failures'].append({'key': f'{args.device}-{target}-{scene}-vulkan-pair',
                                            'error': 'BGFX and wgpu Vulkan RGBA differ'})
    for target in args.targets:
        # Fixed RGB gate: <=2 levels per channel, MAE <=1.0. RGBA exactness of
        # Vulkan backends above remains a separate, stricter check including alpha.
        for scene in SCENES:
            for backend in ('bgfx-opengl', 'bgfx-vulkan', 'wgpu-vulkan'):
                left = indexed.get((target, scene, 'coin-gl'))
                right = indexed.get((target, scene, backend))
                if not left or not right:
                    continue
                metrics = rgb_metrics(args.output_dir / (left['key']+'.ppm'),
                                      args.output_dir / (right['key']+'.ppm'))
                passed = metrics['max'] <= 2 and metrics['mae'] <= 1.0
                results['comparisons'].append({'target': target, 'scene': scene,
                    'backends': ['coin-gl', backend], 'policy': 'top-down RGB max<=2 MAE<=1.0',
                    'metrics': metrics, 'passed': passed})
                if not passed:
                    results['failures'].append({'key': f'{args.device}-{target}-{scene}-{backend}-CoinGL',
                                               'error': 'RGB exceeds the declared gate', 'metrics': metrics})
    result_file.write_text(json.dumps(results, indent=2) + '\n')
    return 1 if results['failures'] or len(results['runs']) != 8*len(args.targets) else 0


if __name__ == '__main__':
    raise SystemExit(main())
