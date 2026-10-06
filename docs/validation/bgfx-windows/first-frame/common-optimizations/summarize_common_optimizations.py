import hashlib
import json
import re
import statistics
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path('H:/Git/coin')
source = root / 'build/coin-render-source'
destination = source / 'docs/validation/bgfx-windows/first-frame/common-optimizations'
destination.mkdir(parents=True, exist_ok=True)
runs = json.loads((root / 'build/common-opt-runs.json').read_text(encoding='utf-8-sig'))

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def archive(path):
    path = Path(path)
    contents = path.read_text(encoding='utf-8-sig')
    (destination / path.name).write_text('\n'.join(line.rstrip() for line in contents.splitlines()) + '\n', encoding='utf-8')

def fields(line):
    return {key: float(value) for key, value in re.findall(r'(\w+)=([\d.eE+-]+)(?:\s|$)', line)}

def records(contents, scope):
    return [fields(line) for line in contents.splitlines()
            if line.startswith('COIN_RENDER_PHASE ' + scope + ' ')]

for run in runs:
    assert run['exit_code'] == 0, run
    contents = (root / run['log']).read_text(encoding='utf-8-sig')
    archive(root / run['log'])
    run['archived_log'] = Path(run['log']).name
    run['first_frame_ms'] = float(re.search(r'_first_frame_ms=([\d.]+)', contents)[1])
    run['warm_median_ms'] = float(re.search(r'^(?:WebGPU|BGFX-\S+) frames=\d+ median_ms=([\d.]+)', contents, re.M)[1])
    run['rgba_fnv64'] = re.search(r'rgba_fnv64=(0x[0-9a-f]+)', contents)[1]
    if run['image']:
        run['image_sha256'] = digest(root / run['image'])
    if run['tracing']:
        validation = records(contents, 'validation_detail')
        composition = records(contents, 'composition_detail')
        run['trace'] = {
            'action_first': records(contents, 'action')[0],
            'target_first': records(contents, 'target')[0],
            'builder': records(contents, 'builder_detail')[0],
            'storage': records(contents, 'plan_storage')[0],
            'validation_passes': validation, 'composition_passes': composition,
            'validation_total_ms': sum(sum(p.values()) for p in validation),
            'vertices_validation_total_ms': sum(p['vertices_ms'] for p in validation),
            'composition_total_ms': sum(sum(p.values()) for p in composition),
            'action_warm': records(contents, 'action')[1:],
        }

comparisons = []
for flavor in ('wgpu', 'bgfx'):
    for api in (('dx12', 'vulkan', 'gl') if flavor == 'wgpu' else ('d3d12', 'vulkan', 'opengl')):
        selected = [r for r in runs if r['flavor'] == flavor and r['api'] == api and r['mode'] == 'static']
        pair = {}
        for variant in ('before', 'after'):
            samples = [r for r in selected if r['variant'] == variant and isinstance(r['sample'], int)]
            assert len(samples) == 3
            pair[variant] = {
                'first_median_ms': statistics.median(r['first_frame_ms'] for r in samples),
                'first_range_ms': [min(r['first_frame_ms'] for r in samples), max(r['first_frame_ms'] for r in samples)],
                'warm_median_ms': statistics.median(r['warm_median_ms'] for r in samples),
                'warm_8_control_ms': next(r['warm_median_ms'] for r in selected if r['variant'] == variant and r['sample'] == 'warm'),
                'image_sha256': samples[0]['image_sha256'],
                'rgba_fnv64': samples[0]['rgba_fnv64'],
                'trace': next(r['trace'] for r in selected if r['variant'] == variant and r['tracing']),
            }
        equal = pair['before']['image_sha256'] == pair['after']['image_sha256']
        assert equal and pair['before']['rgba_fnv64'] == pair['after']['rgba_fnv64'], (flavor, api)
        comparisons.append({'flavor': flavor, 'api': api, **pair, 'images_equal': equal,
            'first_reduction_percent': 100 * (1 - pair['after']['first_median_ms'] / pair['before']['first_median_ms'])})

updates = []
for flavor in ('wgpu', 'bgfx'):
    for mode in ('camera', 'material'):
        before = next(r for r in runs if r['flavor'] == flavor and r['mode'] == mode and r['variant'] == 'before')
        after = next(r for r in runs if r['flavor'] == flavor and r['mode'] == mode and r['variant'] == 'after')
        assert before['image_sha256'] == after['image_sha256'] and before['rgba_fnv64'] == after['rgba_fnv64'], (flavor, mode)
        updates.append({'flavor': flavor, 'api': 'vulkan', 'mode': mode, 'buildings': 10000,
            'images_equal': True, 'image_sha256': after['image_sha256'],
            'before_first_ms': before['first_frame_ms'], 'after_first_ms': after['first_frame_ms'],
            'before_warm_ms': before['warm_median_ms'], 'after_warm_ms': after['warm_median_ms']})

tests = {}
for name in ('common-opt-core-tests', 'common-opt-wgpu-vulkan-tests', 'common-opt-wgpu-vulkan-rerun-tests',
             'common-opt-wgpu-gl-tests', 'common-opt-wgpu-dx12-tests', 'common-opt-bgfx-tests', 'common-opt-bgfx-shadow-tests'):
    xml = ET.parse(root / f'build/{name}.xml').getroot()
    cases = {case.attrib['name']: 'failed' if case.find('failure') is not None else
             'skipped' if case.find('skipped') is not None else 'passed' for case in xml.iter('testcase')}
    tests[name] = {'total': len(cases), 'passed': sum(v == 'passed' for v in cases.values()),
                  'failed': sum(v == 'failed' for v in cases.values()),
                  'skipped': sum(v == 'skipped' for v in cases.values()), 'cases': cases}
    archive(root / f'build/{name}.log')
    archive(root / f'build/{name}.xml')
merged = dict(tests['common-opt-wgpu-vulkan-tests']['cases'])
merged.update(tests['common-opt-wgpu-vulkan-rerun-tests']['cases'])
assert all(v == 'passed' for v in merged.values())
tests['wgpu_vulkan_final'] = {'total': len(merged), 'passed': len(merged),
    'method': '99 initially passed; representation-sensitive ActionTest changed to compare the expanded primitive stream, then ActionTest and five regression cases passed. Original failed log preserved.'}
bgfx_merged = dict(tests['common-opt-bgfx-tests']['cases'])
bgfx_merged.update(tests['common-opt-bgfx-shadow-tests']['cases'])
assert all(v == 'passed' for v in bgfx_merged.values())
tests['bgfx_final'] = {'total': len(bgfx_merged), 'passed': len(bgfx_merged), 'skipped': 0,
    'method': '131 initially passed; 32 explicitly gated GPU shadow cases rerun with COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1. Original skipped log preserved.'}
for name in ('common-opt-wgpu-gl-tests', 'common-opt-wgpu-dx12-tests', 'common-opt-bgfx-tests'):
    assert tests[name]['failed'] == 0

hashes = {}
for flavor in ('wgpu', 'bgfx'):
    for variant in ('before', 'after'):
        directory = root / (f'build/common-opt-baseline/{flavor}' if variant == 'before' else f'build/coin-render-{flavor}-msvc/bin')
        hashes[f'{flavor}-{variant}'] = {name: digest(directory / name) for name in
            ('Coin4.dll', 'CoinRender4.dll', 'coin_render_gl_benchmark.exe')}
    assert hashes[f'{flavor}-before']['Coin4.dll'] == hashes[f'{flavor}-after']['Coin4.dll']

for name in ('measure_common_optimizations.ps1', 'qualify_common_optimizations.ps1',
             'summarize_common_optimizations.py', 'common-opt-cross-api-tests.txt',
             'common-opt-bgfx-core-tests.log', 'common-opt-wgpu-build.log', 'common-opt-bgfx-build.log',
             'common-opt-action-wgpu-build.log', 'common-opt-action-bgfx-build.log',
             'install_common_optimizations.ps1', 'common-opt-wgpu-install.log', 'common-opt-bgfx-install.log',
             'common-opt-install-verification.json', 'qualify_common_bgfx_shadows.ps1', 'common-opt-bgfx-shadow-tests.txt'):
    archive(root / 'build' / name)

summary = {'date': '2026-10-03', 'baseline_commit': '3633e6a5eb',
    'scope': 'Shared CoinRender float validation, enabled-unit handling, composition reuse and native untextured Cube capture.',
    'method': {'paired_new_processes_per_variant_per_api': 3, 'size': [1024, 1024], 'buildings': 40000,
        'warmup_frames': 1, 'measured_warm_frames': 3,
        'warm_control': {'warmup': 4, 'frames': 8},
        'trace': 'separate traced process; CPU wall times, nested phase intervals must not be added',
        'updates': 'single before/after control per mode/backend, 10000 buildings; image equivalence, not a performance distribution',
        'limits': 'single Windows adapter/driver; OS and shader caches retained, no reboot; color + synchronous RGBA copy; first action excludes process/scene load; serial GPU processes'},
    'comparisons': comparisons, 'update_controls': updates, 'tests': tests,
    'runs': runs, 'binary_sha256': hashes,
    'installation': json.loads((root / 'build/common-opt-install-verification.json').read_text(encoding='utf-8-sig')),
    'bgfx_geometry_limit': 'BGFX lowering still expands indexed vertices. Shared CPU capture shrinks, but BGFX output/upload vertex count does not.'}
(destination / 'common-optimizations-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for c in comparisons:
    print(c['flavor'], c['api'], 'first', c['before']['first_median_ms'], '->', c['after']['first_median_ms'],
          'reduction%', round(c['first_reduction_percent'], 2), 'warm8', c['before']['warm_8_control_ms'], '->', c['after']['warm_8_control_ms'])
    for variant in ('before', 'after'):
        t = c[variant]['trace']
        print(' ', variant, 'capture', t['action_first']['traversal_ms'], 'validation', len(t['validation_passes']), round(t['validation_total_ms'], 2),
              'composition', len(t['composition_passes']), round(t['composition_total_ms'], 2), 'storage', t['storage'])
print('Tests:', {name: {k: v for k, v in result.items() if k != 'cases'} for name, result in tests.items()})
print('Updates:', updates)
