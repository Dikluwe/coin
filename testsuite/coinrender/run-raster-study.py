#!/usr/bin/env python3
"""Run manual raster diagnostics sequentially and freeze evidence, never qualify gates."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--adapter', choices=['amd', 'nvidia'], required=True)
parser.add_argument('--api', choices=['vulkan', 'gl'], required=True)
parser.add_argument('--backend', choices=['wgpu', 'bgfx'], default='wgpu')
parser.add_argument('--allow-unidentified-gl', action='store_true',
                    help='Collect BGFX OpenGL diagnostics with unknown vendor; never confirm hardware identity')
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
build = args.build.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env.update(WGPU_BACKEND=args.api, COIN_BGFX_RENDERER='opengl' if args.api=='gl' else 'vulkan',
           COIN_GLX_PIXMAP_DIRECT_RENDERING='1', COIN_GLXGLUE_NO_PBUFFERS='1')
if args.adapter == 'amd':
    env.update(VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',
               VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/radeon_icd.json',
               __GLX_VENDOR_LIBRARY_NAME='mesa',
               __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json')
    env.pop('__NV_PRIME_RENDER_OFFLOAD', None)
else:
    env.update(VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',
               VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',
               __NV_PRIME_RENDER_OFFLOAD='1', __GLX_VENDOR_LIBRARY_NAME='nvidia',
               __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json')
runner = build / 'bin/CoinRenderRasterStudy'
if not runner.is_file():
    parser.error('build CoinRenderRasterStudy first')
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
manifest = dict(backend=args.backend, adapter=args.adapter, api=args.api, source_head=subprocess.check_output(
    ['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip(),
    environment={k: env[k] for k in ['DISPLAY', 'WGPU_BACKEND', 'COIN_BGFX_RENDERER', 'VK_DRIVER_FILES',
        'VK_ICD_FILENAMES', '__GLX_VENDOR_LIBRARY_NAME', '__EGL_VENDOR_LIBRARY_FILENAMES',
        'COIN_GLX_PIXMAP_DIRECT_RENDERING', 'COIN_GLXGLUE_NO_PBUFFERS'] if k in env},
    source_hashes={str(p.relative_to(source)): sha(p) for p in [
        source/'testsuite/coinrender/CoinRenderRasterStudy.cpp', source/'testsuite/CMakeLists.txt',
        source/'src/rendering/coinrender/CoinRenderStrokeCore.h',
        source/'src/rendering/coinrender/CoinRenderPolygonStyleCore.h',
        source/'src/rendering/coinrender/CoinRenderCpuReferenceBackend.cpp',
        source/'src/rendering/SoGL.cpp', source/'src/misc/SoGenerate.cpp', Path(__file__).resolve()]},
    binary_hashes={str(p): sha(p) for p in [runner, build/'lib/libCoinRender.so', build/'lib/libCoin.so']}, runs=[])
for scene in ['sphere', 'cone', 'camera']:
    cases = [('lines', 1, 'native'), ('lines', 4, 'native'), ('filled', 4, 'native')]
    if scene == 'camera':
        cases = [('filled', 1, m) for m in ['native', 'float', 'double', 'snap8']]
    for style, width, mode in cases:
        profile = f'{args.adapter}-{args.api}' if args.backend=='wgpu' else f'bgfx-{args.adapter}-{args.api}'
        label = f'{profile}-{scene}-{style}-w{width}-{mode}'
        directory = output/label
        if directory.exists():
            parser.error(f'refusing to overwrite evidence: {directory}')
        directory.mkdir()
        command = [str(runner), scene, '5' if scene == 'camera' else '0', str(width), style, mode, str(directory)]
        if scene != 'camera' and style == 'lines' and width == 4:
            command.append('--polygons')
        with (directory/'run.log').open('w') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        evidence = (directory/'run.log').read_text()
        identity = re.search(r'backend=(\d+) renderer=(\d+) vendor_id=(\d+)', evidence)
        expected = (3 if args.backend=='bgfx' else 1, 2 if args.api=='gl' else 1,
                    4098 if args.adapter=='amd' else 4318)
        observed = tuple(map(int, identity.groups())) if identity else None
        confirmed = observed == expected
        api_confirmed = bool(observed and observed[:2] == expected[:2])
        unidentified_gl = bool(args.allow_unidentified_gl and args.backend=='bgfx' and args.api=='gl'
                               and api_confirmed and observed[2]==0)
        manifest['runs'].append(dict(label=label, command=command, exit_code=result.returncode,
            identity_confirmed=confirmed, api_backend_confirmed=api_confirmed,
            unidentified_gl_collected=unidentified_gl,
            files={p.name: sha(p) for p in directory.iterdir() if p.is_file()}))
        (output/f'{profile}-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        print(f'{label}: exit={result.returncode}', flush=True)
        if result.returncode or not (confirmed or unidentified_gl):
            print('Incomplete or wrong GPU/API/backend; inspect run.log.')
            raise SystemExit(result.returncode or 1)
print('Campaign completed; manifests distinguish confirmed hardware from unidentified OpenGL diagnostics.')
