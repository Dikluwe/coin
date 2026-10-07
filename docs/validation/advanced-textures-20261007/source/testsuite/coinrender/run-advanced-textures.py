#!/usr/bin/env python3
"""Sequential hardware validation; every exit, command and log is preserved."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--wgpu-build', type=Path, required=True)
parser.add_argument('--bgfx-build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--profiles', default='wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-opengl,bgfx-amd-vulkan,bgfx-nvidia-vulkan,bgfx-opengl')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
result = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
          'source_diff_sha256': hashlib.sha256(subprocess.check_output(['git', 'diff', 'HEAD'], cwd=root)).hexdigest(), 'runs': []}
for profile in args.profiles.split(','):
    backend = profile.split('-')[0]
    build = args.wgpu_build if backend == 'wgpu' else args.bgfx_build
    api = 'opengl' if profile.endswith('opengl') else 'vulkan'
    gpu = 'nvidia' if 'nvidia' in profile else 'amd'
    env = dict(os.environ, DISPLAY=os.environ.get('DISPLAY', ':0'),
               COIN_GLX_PIXMAP_DIRECT_RENDERING='1', COIN_GLXGLUE_NO_PBUFFERS='1',
               COIN_RENDER_REQUIRE_GL_REFERENCE='1', WGPU_BACKEND='gl' if api == 'opengl' else 'vulkan', COIN_BGFX_RENDERER=api)
    env['VK_DRIVER_FILES'] = env['VK_ICD_FILENAMES'] = '/usr/share/vulkan/icd.d/' + ('nvidia_icd.json' if gpu == 'nvidia' else 'radeon_icd.json')
    env['__GLX_VENDOR_LIBRARY_NAME'] = 'nvidia' if gpu == 'nvidia' else 'mesa'
    env['__EGL_VENDOR_LIBRARY_FILENAMES'] = '/usr/share/glvnd/egl_vendor.d/' + ('10_nvidia.json' if gpu == 'nvidia' else '50_mesa.json')
    if gpu == 'nvidia': env['__NV_PRIME_RENDER_OFFLOAD'] = '1'
    else: env.pop('__NV_PRIME_RENDER_OFFLOAD', None)
    if 'XAUTHORITY' not in env and Path.home().joinpath('.Xauthority').exists(): env['XAUTHORITY'] = str(Path.home() / '.Xauthority')
    cases = [('advanced', ['CoinRenderAdvancedTextureTest', '--gpu']), ('sampling', ['CoinRenderTextureSamplingTest', '--gpu']),
             ('staged-mips', ['CoinRenderRttProfileTest', '--mips']), ('direct-mips', ['CoinRenderRttProfileTest', '--mips-direct'])]
    for case, command in cases:
        cmd = [str(build / 'bin' / command[0]), *command[1:]]
        log = args.output / (profile + '-' + case + '.log')
        try:
            with log.open('w') as stream: run = subprocess.run(cmd, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=200)
            code = run.returncode
        except subprocess.TimeoutExpired: code = 124
        content = log.read_text(errors='replace')
        result['runs'].append({'profile': profile, 'case': case, 'command': cmd,
            'environment': {k: env[k] for k in ['DISPLAY', 'XAUTHORITY', 'VK_DRIVER_FILES', '__GLX_VENDOR_LIBRARY_NAME', '__EGL_VENDOR_LIBRARY_FILENAMES', 'WGPU_BACKEND', 'COIN_BGFX_RENDERER', 'COIN_GLX_PIXMAP_DIRECT_RENDERING', 'COIN_GLXGLUE_NO_PBUFFERS', 'COIN_RENDER_REQUIRE_GL_REFERENCE'] if k in env},
            'exit': code, 'status': 'PASS' if code == 0 else 'SKIP' if code == 77 else 'FAIL',
            'adapter_report': next((line for line in content.splitlines() if line.startswith('adapter=')), None),
            'log': log.name, 'sha256': hashlib.sha256(log.read_bytes()).hexdigest(),
            'controls': re.findall(r'Advanced textures controls=(\d+)', content),
            'failures': [line for line in content.splitlines() if line.startswith('FAIL ') or 'texture pixels' in line]})
        (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
        print(profile, case, result['runs'][-1]['status'], flush=True)
