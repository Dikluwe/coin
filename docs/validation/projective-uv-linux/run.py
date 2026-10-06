import argparse
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path('/tmp/coin-render-first-frame')
OUT = pathlib.Path('/tmp/coin-projective-uv-validation')
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'scripts/coinrender'))
from run_animation_benchmark import environment

BUILDS = {
    'wgpu-vulkan': pathlib.Path('/tmp/coin-render-first-frame-wgpu'),
    'bgfx-vulkan': pathlib.Path('/tmp/coin-render-first-frame-bgfx'),
    'bgfx-opengl': pathlib.Path('/tmp/coin-render-first-frame-bgfx'),
}
parser = argparse.ArgumentParser()
parser.add_argument('phase', choices=['capture', 'gpu', 'regression', 'core', 'screen', 'marker', 'fragment', 'shadow', 'multidevice', 'drawstyle'])
parser.add_argument('variant', choices=list(BUILDS))
args = parser.parse_args()
build = BUILDS[args.variant]
env = environment(build, args.variant, 'nvidia')
env['COIN_GLX_PIXMAP_DIRECT_RENDERING'] = '1'
env['COIN_RENDER_REQUIRE_GL_REFERENCE'] = '1'
env['COIN_BGFX_TRANSPARENCY'] = 'auto' if args.phase in ['regression', 'core'] else 'object'
if args.phase in ['capture', 'gpu']:
    cmd = [str(build / 'bin/CoinRenderProjectiveUvTest'), '--' + args.phase]
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
             'CoinRenderDrawStyleTest', 'CoinRenderIndexedFastPathTest', 'CoinRenderProjectiveUvCaptureTest',
             'CoinBgfxCoreTest', 'CoinRenderTransparencyTest',
             'CoinRenderPeelingTest', 'CoinRenderSceneTextureTest',
             'CoinRenderScreenContentCaptureTest', 'CoinRenderFragmentPolicyCaptureTest']
    if args.phase == 'core':
        names.remove('CoinRenderProjectiveUvCaptureTest')
    if args.variant.startswith('wgpu'):
        names.append('CoinWgpuFfiFrameTest')
    cmd = ['ctest', '--test-dir', str(build), '-R', '^(' + '|'.join(names) + ')$',
           '--output-on-failure', '-j', '1']
log = OUT / (args.phase + '-' + args.variant + '.log')
start = time.monotonic()
with log.open('w') as stream:
    result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
record = {
    'phase': args.phase, 'variant': args.variant, 'command': cmd,
    'exit_code': result.returncode, 'seconds': round(time.monotonic() - start, 3),
    'log': log.name,
    'environment': {key: env.get(key) for key in [
        'COIN_GLX_PIXMAP_DIRECT_RENDERING', 'COIN_RENDER_REQUIRE_GL_REFERENCE', 'COIN_BGFX_RENDERER',
        'COIN_BGFX_TRANSPARENCY', 'WGPU_BACKEND', 'VK_ICD_FILENAMES',
        '__NV_PRIME_RENDER_OFFLOAD', '__GLX_VENDOR_LIBRARY_NAME', 'LD_LIBRARY_PATH']},
}
(OUT / (args.phase + '-' + args.variant + '.json')).write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, ensure_ascii=False))
lines = log.read_text(errors='replace').splitlines()
print('\n'.join(lines[-3:] if result.returncode == 0 else lines[-20:]))
sys.exit(result.returncode)
