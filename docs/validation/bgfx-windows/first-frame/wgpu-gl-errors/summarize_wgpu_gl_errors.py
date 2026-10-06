from pathlib import Path
from PIL import Image, ImageChops
import hashlib
import json
import re
import xml.etree.ElementTree as ET

root = Path('H:/Git/coin')
build = root / 'build'
source = build / 'coin-render-source'
evidence = source / 'docs/validation/bgfx-windows/first-frame/wgpu-gl-errors'
evidence.mkdir(parents=True, exist_ok=True)

def digest(path):
    data = path.read_bytes()
    return {'path': str(path.relative_to(root)), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

def junit(path):
    suite = ET.parse(path).getroot()
    cases = suite.findall('testcase')
    return {'tests': len(cases), 'failed': [c.get('name') for c in cases if c.find('failure') is not None],
            'skipped': [c.get('name') for c in cases if c.find('skipped') is not None or c.get('status') == 'notrun'],
            'seconds': float(suite.get('time')), 'timestamp': suite.get('timestamp')}

baseline = junit(build / 'wgpu-gl-errors-baseline/wgpu-large-final-gl-tests.xml')
qualified = {api: junit(build / f'wgpu-gl-errors-final-{api}-tests.xml') for api in ['gl', 'vulkan', 'dx12']}
for api, result in qualified.items():
    assert result['tests'] == (100 if api == 'gl' else 29)
    assert not result['failed'] and not result['skipped'], (api, result)
gl_cases = {c.get('name') for c in ET.parse(build / 'wgpu-gl-errors-final-gl-tests.xml').getroot().findall('testcase')}
assert len(baseline['failed']) == 19 and set(baseline['failed']) <= gl_cases

cities = {}
for api in qualified:
    log = (build / f'wgpu-gl-errors-{api}-city.log').read_text(encoding='utf-8-sig')
    current = build / f'large-scenes/wgpu-gl-errors-{api}-city.ppm'
    previous = build / f'large-scenes/wgpu-final-{api}-after-warm.ppm'
    with Image.open(previous) as a, Image.open(current) as b:
        assert a.size == b.size == (1024, 1024)
        assert ImageChops.difference(a.convert('RGB'), b.convert('RGB')).getbbox() is None, api
    value = lambda pattern: re.search(pattern, log).group(1)
    assert value(r'rgba_fnv64=(\S+)') == '0x6714299260985122'
    cities[api] = {'first_frame_ms': float(value(r'WebGPU_first_frame_ms=([\d.]+)')),
                   'warm_median_ms': float(value(r'WebGPU frames=8 median_ms=([\d.]+)')),
                   'rgba_fnv64': value(r'rgba_fnv64=(\S+)'), 'changed_pixels_vs_baseline_827603713e': 0,
                   'image': digest(current), 'reference_image': digest(previous)}

rust_log = (build / 'wgpu-gl-errors-final-rust-tests.log').read_text(encoding='utf-8-sig')
rust_passed = sum(map(int, re.findall(r'test result: ok\. (\d+) passed; 0 failed;', rust_log)))
assert rust_passed == 27, rust_passed
assert 'backend: Gl' in rust_log, 'GL depth adapter provenance missing'
binaries = {'before': {}, 'after': {}, 'installed': {}}
for name in ['Coin4.dll', 'CoinRender4.dll']:
    binaries['before'][name] = digest(build / 'wgpu-gl-errors-baseline' / name)
    binaries['after'][name] = digest(build / 'coin-render-wgpu-msvc/bin' / name)
    binaries['installed'][name] = digest(build / 'coin-render-install/bin' / name)
    assert binaries['after'][name]['sha256'] == binaries['installed'][name]['sha256']
assert binaries['before']['Coin4.dll']['sha256'] == binaries['after']['Coin4.dll']['sha256']

summary = {'source_baseline': '827603713e', 'scope': 'Local wgpu/OpenGL depth readback, depth snapshots and peeling fixes; private protocol 42 unchanged.',
           'platform': {'os': 'Windows 10 Pro 19045', 'cpu': 'Intel Core i5-4670K', 'ram_gib': 16,
                        'gpu': 'NVIDIA GeForce GTX 1060 6GB', 'driver': '581.08', 'wgpu': '24', 'configuration': 'MSVC Release RUST_BRIDGE'},
           'baseline_gl': baseline, 'qualified': qualified, 'resolved_baseline_failures': baseline['failed'],
           'rust_tests_passed': rust_passed, 'depth_bits_gpu_apis': ['gl', 'vulkan', 'dx12'], 'binaries': binaries, 'city': cities,
           'method': {'gpu_runs_serial': True, 'mandatory_flags': ['COIN_RENDER_REQUIRE_GL_REFERENCE=1', 'COIN_WGPU_REQUIRE_GL_REFERENCE=1', 'COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1'],
                      'cross_api_extra_flag': 'COIN_WGPU_CAMERA_BINDINGS=1', 'city': 'One process per API, 40,000 buildings at 1024x1024, 4 warmup frames and 8 measured frames, color readback. Driver/system caches retained; no reboot.',
                      'diagnostic_run': 'Initial depth/shader fix passed focused tests, but peeling colors remained wrong because GLES depth texture copy attaches COLOR_ATTACHMENT0. The partial 100-test diagnostic run was stopped after isolating this cause; it is not a complete qualification.'}}
(evidence / 'wgpu-gl-errors-summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
files = [build / 'wgpu-gl-errors-baseline/wgpu-large-final-gl-tests.log', build / 'wgpu-gl-errors-baseline/wgpu-large-final-gl-tests.xml',
         build / 'wgpu-gl-depth-focused.log', build / 'wgpu-gl-errors-full-tests.log', build / 'wgpu-gl-errors-final-build.log',
         build / 'wgpu-gl-errors-final-rust-tests.log', build / 'wgpu-gl-errors-peeling-tests.log', build / 'wgpu-gl-errors-qualify.ps1',
         build / 'summarize_wgpu_gl_errors.py', build / 'wgpu-gl-errors-install.log']
files += [build / f'wgpu-gl-errors-final-{api}-tests.{ext}' for api in qualified for ext in ['log', 'xml']]
files += [build / f'wgpu-gl-errors-depth-{api}-tests.log' for api in ['vulkan', 'dx12']]
files += [build / f'wgpu-gl-errors-{api}-city.log' for api in qualified]
for path in files:
    content = '\n'.join(line.rstrip() for line in path.read_text(encoding='utf-8-sig').splitlines()).rstrip() + '\n'
    (evidence / path.name).write_text(content, encoding='utf-8')
print(json.dumps({'qualified': qualified, 'rust_passed': rust_passed, 'city': cities}, indent=2))
