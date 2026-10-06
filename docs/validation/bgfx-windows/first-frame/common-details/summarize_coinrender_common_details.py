import hashlib
import json
import re
import statistics
from pathlib import Path

root = Path('H:/Git/coin')
source = root / 'build/coin-render-source'
destination = source / 'docs/validation/bgfx-windows/first-frame/common-details'
destination.mkdir(parents=True, exist_ok=True)
runs = json.loads((root / 'build/common-details-runs.json').read_text(encoding='utf-8-sig'))

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def fields(line):
    return {key: float(value) for key, value in re.findall(r'(\w+)=([\d.eE+-]+)(?:\s|$)', line)}

def records(text, scope):
    return [fields(line) for line in text.splitlines()
            if line.startswith('COIN_RENDER_PHASE ' + scope + ' ')]

def archive(path):
    path = Path(path)
    contents = path.read_text(encoding='utf-8-sig')
    (destination / path.name).write_text('\n'.join(line.rstrip() for line in contents.splitlines()) + '\n', encoding='utf-8')

for run in runs:
    log = root / run['log']
    contents = log.read_text(encoding='utf-8-sig')
    archive(log)
    run['archived_log'] = log.name
    run['first_frame_ms'] = float(re.search(r'_first_frame_ms=([\d.]+)', contents)[1])
    run['warm_median_ms'] = float(re.search(r'^(?:WebGPU|BGFX-\S+) frames=3 median_ms=([\d.]+)', contents, re.M)[1])
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
            'validation_passes': validation,
            'composition_passes': composition,
            'validation_total_ms': sum(sum(p.values()) for p in validation),
            'vertices_validation_total_ms': sum(p['vertices_ms'] for p in validation),
            'composition_total_ms': sum(sum(p.values()) for p in composition),
            'action_warm': records(contents, 'action')[1:],
        }

comparisons = []
for flavor in ('wgpu', 'bgfx'):
    for api in (('dx12', 'vulkan', 'gl') if flavor == 'wgpu' else ('d3d12', 'vulkan', 'opengl')):
        selected = [run for run in runs if run['flavor'] == flavor and run['api'] == api
                    and run['buildings'] == 40000 and run['side'] == 1024]
        before = [run for run in selected if run['variant'] == 'baseline']
        after = [run for run in selected if not run['tracing'] and run['variant'] == 'instrumented']
        trace = next(run['trace'] for run in selected if run['tracing'])
        comparisons.append({'flavor': flavor, 'api': api,
            'baseline_first_median_ms': statistics.median(run['first_frame_ms'] for run in before),
            'instrumented_first_median_ms': statistics.median(run['first_frame_ms'] for run in after),
            'baseline_first_range_ms': [min(run['first_frame_ms'] for run in before), max(run['first_frame_ms'] for run in before)],
            'instrumented_first_range_ms': [min(run['first_frame_ms'] for run in after), max(run['first_frame_ms'] for run in after)],
            'instrumented_warm_median_ms': statistics.median(run['warm_median_ms'] for run in after),
            'images_equal': before[0]['image_sha256'] == after[0]['image_sha256'],
            'image_sha256': after[0]['image_sha256'], 'trace': trace})

binary_hashes = {}
for flavor in ('wgpu', 'bgfx'):
    for variant in ('baseline', 'instrumented'):
        directory = root / (f'build/first-frame-details-baseline/{flavor}' if variant == 'baseline'
                            else f'build/coin-render-{flavor}-msvc/bin')
        binary_hashes[f'{flavor}-{variant}'] = {
            name: digest(directory / name) for name in ('Coin4.dll', 'CoinRender4.dll', 'coin_render_gl_benchmark.exe')}

for name in ('measure_coinrender_common_details.ps1', 'summarize_coinrender_common_details.py',
             'details-common-tests.log', 'details-common-tests.xml', 'details-common-tests-build.log',
             'details-finite-probe.log', 'details-finite-build.log', 'details-finite-configure.log',
             'details-frameplan-symbols.log', 'details-wgpu-build.log', 'details-bgfx-build.log'):
    archive(root / 'build' / name)
for name in ('finite_check_probe.cpp', 'CMakeLists.txt'):
    archive(root / 'build/finite_check_probe' / name)

summary = {'date': '2026-10-02', 'scope': 'Shared CoinRender capture, frame-plan validation and composition; backend times are context only.',
    'method': {'new_process_per_sample': True, 'untraced_samples_per_variant_per_api': 3,
        'warmup_frames': 1, 'measured_warm_frames': 3,
        'phase_timings': 'one separate traced process per configuration, CPU wall times; parent and child intervals must not be added together',
        'probe': 'isolated MSVC Release finite-check diagnostic, not a production optimization',
        'limits': 'single adapter/driver; OS and driver caches retained; no reboot or shader-cache purge; since_main excludes process loader'},
    'comparisons': comparisons, 'runs': runs, 'binary_sha256': binary_hashes,
    'finite_probe': (root / 'build/details-finite-probe.log').read_text().strip()}
(destination / 'common-details-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for item in comparisons:
    trace = item['trace']
    print(item['flavor'], item['api'], 'capture', trace['action_first']['traversal_ms'],
          'validations', len(trace['validation_passes']), round(trace['validation_total_ms'], 2),
          'composition', round(trace['composition_total_ms'], 2),
          'first', item['instrumented_first_median_ms'], 'image_equal', item['images_equal'])
for run in runs:
    if run['tracing'] and (run['buildings'] != 40000 or run['side'] != 1024):
        print('scale', run['flavor'], run['buildings'], run['side'], run['trace']['action_first'],
              'validation_total', round(run['trace']['validation_total_ms'], 2))
