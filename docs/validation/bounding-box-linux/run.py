import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path('/tmp/coin-render-first-frame')
OUT = pathlib.Path('/tmp/coin-bounding-box-validation')
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'scripts/coinrender'))
from run_animation_benchmark import environment

BUILDS = {
    'wgpu-vulkan': pathlib.Path('/tmp/coin-render-first-frame-wgpu'),
    'bgfx-vulkan': pathlib.Path('/tmp/coin-render-first-frame-bgfx'),
    'bgfx-opengl': pathlib.Path('/tmp/coin-render-first-frame-bgfx'),
}
parser = argparse.ArgumentParser()
parser.add_argument('phase', choices=['capture', 'gpu', 'regression', 'core', 'screen', 'marker', 'fragment', 'shadow', 'multidevice', 'drawstyle', 'uv', 'depth'])
parser.add_argument('variant', choices=list(BUILDS))
args = parser.parse_args()
build = BUILDS[args.variant]
env = environment(build, args.variant, 'nvidia')
env['COIN_GLX_PIXMAP_DIRECT_RENDERING'] = '1'
env['COIN_RENDER_REQUIRE_GL_REFERENCE'] = '1'
env['COIN_BGFX_TRANSPARENCY'] = 'auto' if args.phase in ['regression', 'core', 'depth'] else 'object'
if args.phase == 'depth':
    cmd = ['ctest', '--test-dir', str(build), '-R', '^CoinRenderDepthContractTest$', '--output-on-failure', '-j', '1']
elif args.phase == 'uv':
    cmd = [str(build / 'bin/CoinRenderProjectiveUvTest'), '--gpu']
elif args.phase in ['capture', 'gpu']:
    cmd = [str(build / 'bin/CoinRenderBoundingBoxTest'), '--' + args.phase]
elif args.phase in ['screen', 'marker']:
    name = 'CoinRenderScreenContentTest' if args.phase == 'screen' else 'CoinRenderMarkerSetTest'
    cmd = [str(build / 'bin' / name), '--gpu']
elif args.phase == 'fragment':
    cmd = [str(build / 'bin/CoinRenderFragmentPolicyTest'), '--gpu']
elif args.phase == 'shadow':
    cmd = [str(build / 'bin/CoinRenderShadowReferenceTest')]
elif args.phase == 'drawstyle':
    cmd = [str(build / 'bin/CoinRenderDrawStyleTest')]
elif args.phase == 'multidevice':
    cmd = [str(build / 'bin/CoinWgpuMultiDeviceTest')]
else:
    names = ['CoinRenderActionTest', 'CoinRenderNodeInventoryTest',
             'CoinRenderCompositionTest', 'CoinRenderTextureTest',
             'CoinRenderMultitextureTest', 'CoinRenderDepthContractTest',
             'CoinRenderFrameReuseCoreTest', 'CoinRenderRttOwnershipTest',
             'CoinRenderLightingTest', 'CoinRenderFrameCoreTest', 'CoinRenderPlanAssemblyCoreTest',
             'CoinRenderDrawStyleTest', 'CoinRenderIndexedFastPathTest', 'CoinRenderBoundingBoxCaptureTest',
             'CoinBgfxCoreTest', 'CoinRenderTransparencyTest',
             'CoinRenderPeelingTest', 'CoinRenderSceneTextureTest',
             'CoinRenderScreenContentCaptureTest', 'CoinRenderFragmentPolicyCaptureTest']
    if args.phase == 'core':
        names.remove('CoinRenderBoundingBoxCaptureTest')
    if args.variant.startswith('wgpu'):
        names.append('CoinWgpuFfiFrameTest')
    cmd = ['ctest', '--test-dir', str(build), '-R', '^(' + '|'.join(names) + ')$',
           '--output-on-failure', '-j', '1']
log = OUT / (args.phase + '-' + args.variant + '.log')
binaries = [build / "lib/libCoin.so", build / "lib/libCoinRender.so"]
if pathlib.Path(cmd[0]).is_file():
    binaries.append(pathlib.Path(cmd[0]))
else:
    selection = json.loads(subprocess.check_output(cmd[:1] + ["--show-only=json-v1"] + cmd[1:], text=True))
    binaries.extend(pathlib.Path(test["command"][0]) for test in selection["tests"])
executed_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in binaries}
start = time.monotonic()
with log.open('w') as stream:
    result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
record = {
    'phase': args.phase, 'variant': args.variant, 'command': cmd,
    'exit_code': result.returncode, 'seconds': round(time.monotonic() - start, 3),
    'log': log.name,
    'executed_binaries_sha256': executed_hashes,
    'environment': {key: env.get(key) for key in [
        'COIN_GLX_PIXMAP_DIRECT_RENDERING', 'COIN_RENDER_REQUIRE_GL_REFERENCE', 'COIN_BGFX_RENDERER',
        'COIN_BGFX_TRANSPARENCY', 'WGPU_BACKEND', 'VK_ICD_FILENAMES',
        '__NV_PRIME_RENDER_OFFLOAD', '__GLX_VENDOR_LIBRARY_NAME', 'LD_LIBRARY_PATH']},
}
(OUT / (args.phase + '-' + args.variant + '.json')).write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({key: value for key, value in record.items() if key != "executed_binaries_sha256"}, ensure_ascii=False))
lines = log.read_text(errors='replace').splitlines()
print('\n'.join(lines[-3:] if result.returncode == 0 else lines[-20:]))
sys.exit(result.returncode)
