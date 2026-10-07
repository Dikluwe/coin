#!/usr/bin/env python3
"""Run manual raster diagnostics sequentially and freeze evidence, never qualify gates."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--adapter', choices=['amd', 'nvidia'], required=True)
parser.add_argument('--api', choices=['vulkan', 'gl'], required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
build = args.build.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env.update(WGPU_BACKEND=args.api, COIN_GLX_PIXMAP_DIRECT_RENDERING='1', COIN_GLXGLUE_NO_PBUFFERS='1')
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
manifest = dict(adapter=args.adapter, api=args.api, source_head=subprocess.check_output(
    ['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip(),
    environment={k: env[k] for k in ['DISPLAY', 'WGPU_BACKEND', 'VK_DRIVER_FILES',
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
        label = f'{args.adapter}-{args.api}-{scene}-{style}-w{width}-{mode}'
        directory = output/label
        if directory.exists():
            parser.error(f'refusing to overwrite evidence: {directory}')
        directory.mkdir()
        command = [str(runner), scene, '5' if scene == 'camera' else '0', str(width), style, mode, str(directory)]
        if scene != 'camera' and style == 'lines' and width == 4:
            command.append('--polygons')
        with (directory/'run.log').open('w') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        manifest['runs'].append(dict(label=label, command=command, exit_code=result.returncode,
            files={p.name: sha(p) for p in directory.iterdir() if p.is_file()}))
        (output/f'{args.adapter}-{args.api}-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        print(f'{label}: exit={result.returncode}', flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)
print('Campaign completed; inspect metrics and adapter names, not just exit codes.')
