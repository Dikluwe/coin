import hashlib
import json
from pathlib import Path
import re
import statistics
import xml.etree.ElementTree as ET

root = Path('H:/Git/coin')
build = root / 'build'

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def read(run):
    data = (root / run['log']).read_text(encoding='utf-8-sig', errors='replace')
    assert run['exit_code'] == 0
    result = dict(run)
    result['first_ms'] = float(re.search(r'_first_frame_ms=([\d.]+)', data)[1])
    result['warm_ms'] = float(re.search(r'^BGFX-[^\s]+ frames=\d+ median_ms=([\d.]+)', data, re.M)[1])
    result['rgba_fnv64'] = re.search(r'rgba_fnv64=(0x[\da-f]+)', data)[1]
    result['image_sha256'] = digest(root / run['image'])
    phase = re.search(r'^COIN_RENDER_PHASE bgfx lower_ms=(.*)', data, re.M)
    if phase:
        result['first_backend_trace'] = dict(re.findall(r'(\w+)=([^\s]+)', 'lower_ms=' + phase[1]))
        target = re.search(r'^COIN_RENDER_PHASE target (.*)', data, re.M)
        result['first_target_trace'] = dict(re.findall(r'(\w+)=([^\s]+)', target[1]))
        for label in ('bgfx_program_cache', 'bgfx_geometry'):
            line = re.search(r'^COIN_RENDER_PHASE ' + label + r' (.*)', data, re.M)
            if line:
                result[label] = dict(re.findall(r'(\w+)=([^\s]+)', line[1]))
    return result

runs = json.loads((build / 'bgfx-gl-stages-runs.json').read_text(encoding='utf-8-sig'))
all_runs = [read(run) for run in runs]
comparisons, image_checks = [], []
for api in ('opengl', 'vulkan', 'd3d12'):
    grouped = {}
    for variant in ('before', 'after'):
        samples = [r for r in all_runs if r['api'] == api and r['variant'] == variant
                   and r['mode'] == 'static' and isinstance(r['sample'], int)]
        assert len(samples) == (6 if api == 'opengl' else 3)
        assert all(r['warm_ms'] < 30 for r in samples), 'Warm frame stability gate exceeded'
        warm = next(r for r in all_runs if r['api'] == api and r['variant'] == variant and r['sample'] == 'warm')
        trace = next(r for r in all_runs if r['api'] == api and r['variant'] == variant and r['sample'] == 'trace')
        grouped[variant] = {'sample_count': len(samples), 'first_median_ms': statistics.median(r['first_ms'] for r in samples),
            'first_range_ms': [min(r['first_ms'] for r in samples), max(r['first_ms'] for r in samples)],
            'warm_control_ms': warm['warm_ms'], 'trace': trace['first_backend_trace'],
            'target_trace': trace['first_target_trace']}
    before, after = grouped['before'], grouped['after']
    comparisons.append({'api': api, **grouped,
        'reduction_percent': 100 * (1 - after['first_median_ms'] / before['first_median_ms'])})

for before in (r for r in all_runs if r['variant'] == 'before'):
    after = next(r for r in all_runs if r['variant'] == 'after' and r['api'] == before['api']
                 and r['sample'] == before['sample'] and r['mode'] == before['mode'])
    assert before['image_sha256'] == after['image_sha256']
    assert before['rgba_fnv64'] == after['rgba_fnv64']
    image_checks.append({'api': before['api'], 'mode': before['mode'], 'sample': before['sample'],
                        'sha256': before['image_sha256'], 'rgba_fnv64': before['rgba_fnv64'], 'equal': True})

tests = {}
for name in ('tests', 'lifetime-vulkan', 'lifetime-d3d12'):
    path = build / ('bgfx-gl-stages-' + name + '.xml')
    assert path.exists(), path
    cases = ET.parse(path).findall('.//testcase')
    tests[name] = {'total': len(cases), 'failed': sum(c.find('failure') is not None for c in cases),
                   'skipped': sum(c.find('skipped') is not None for c in cases)}
    assert not tests[name]['failed'] and not tests[name]['skipped']
assert tests['tests']['total'] == 50

binary_hashes = {v: {f: digest(build / directory / f) for f in ('Coin4.dll', 'CoinRender4.dll', 'coin_render_gl_benchmark.exe')}
    for v, directory in (('before', 'bgfx-gl-stages-baseline'), ('after', 'coin-render-bgfx-msvc/bin'))}
assert binary_hashes['before']['Coin4.dll'] == binary_hashes['after']['Coin4.dll']
assert binary_hashes['before']['coin_render_gl_benchmark.exe'] == binary_hashes['after']['coin_render_gl_benchmark.exe']
assert binary_hashes['before']['CoinRender4.dll'] != binary_hashes['after']['CoinRender4.dll']

summary = {'date': '2026-10-03', 'baseline_commit': '9fd10a080b', 'scene': 'city-40000.iv', 'size': [1024, 1024],
    'measurement': 'First render plus synchronous RGBA copy; serial fresh processes; driver/system caches retained; no tracing in timed medians.',
    'scope': 'BGFX lowering reuses per-draw material/normal work and per-frame finite-color checks. Large qualified untextured opaque OpenGL uses an exact 124-byte attribute prefix. GL programs use a bounded process-memory cache, depth resources are lazy, and synchronous readback publishes by storage swap.',
    'comparisons': comparisons, 'image_checks': image_checks, 'tests': tests, 'binary_hashes': binary_hashes, 'runs': all_runs,
    'pilot_rounds': 'Initial noncompact and compact tracing pilots preserved separately; initial Core compact test fixture corrected from SCREEN_DOOR and invalid layer-zero depth barrier. Final integrated suite verifies visible GPU geometry and exact compact/full attributes.'}
ablation_runs = json.loads((build / 'bgfx-gl-stages-ablation-runs.json').read_text(encoding='utf-8-sig'))
ablations = []
for mode in ('compact-cache-off', 'full-layout'):
    selected = [r for r in ablation_runs if r['mode'] == mode]
    assert len(selected) == 3
    times = []
    for run in selected:
        assert run['exit_code'] == 0
        data = (root / run['log']).read_text(encoding='utf-8-sig', errors='replace')
        times.append(float(re.search(r'_first_frame_ms=([\d.]+)', data)[1]))
        reference = next(r for r in all_runs if r['api'] == 'opengl' and r['variant'] == 'after' and r['sample'] == 1 and r['mode'] == 'static')
        assert digest(root / run['image']) == reference['image_sha256']
        assert re.search(r'rgba_fnv64=(0x[\da-f]+)', data)[1] == reference['rgba_fnv64']
    ablations.append({'mode': mode, 'first_median_ms': statistics.median(times), 'samples_ms': times})
summary['ablations'] = ablations
summary['ablation_runs'] = ablation_runs
pipeline_runs = [read(r) for r in json.loads((build / 'bgfx-gl-stages-pipeline-runs.json').read_text(encoding='utf-8-sig'))]
pipeline_checks = []
for before in (r for r in pipeline_runs if r['variant'] == 'before'):
    after = next(r for r in pipeline_runs if r['variant'] == 'after' and r['depth'] == before['depth'] and r['mode'] == before['mode'])
    assert before['image_sha256'] == after['image_sha256']
    assert before['rgba_fnv64'] == after['rgba_fnv64']
    assert int(after['first_backend_trace']['readback_pipeline_depth']) == before['depth']
    pipeline_checks.append({'depth': before['depth'], 'mode': before['mode'], 'equal': True, 'sha256': before['image_sha256'], 'rgba_fnv64': before['rgba_fnv64']})
summary['pipeline_runs'] = pipeline_runs
summary['pipeline_image_checks'] = pipeline_checks
summary['pipeline_test_limit'] = 'CoinBgfxReadbackModes requires immediate pixels and fails its screen-door check with pipeline depth 2 both before and after. Dynamic camera/material benchmark image pairs instead validate preservation of the pipelined output contract for depths 2 and 3.'
installation_path = build / 'bgfx-gl-stages-install-verification.json'
if installation_path.exists():
    summary['installation'] = json.loads(installation_path.read_text(encoding='utf-8-sig'))
(build / 'bgfx-gl-stages-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for row in comparisons:
    print(row['api'], row['before']['first_median_ms'], '->', row['after']['first_median_ms'],
          'gain%', round(row['reduction_percent'], 2), 'warm', row['before']['warm_control_ms'], '->', row['after']['warm_control_ms'])
print('Exact image pairs:', len(image_checks))
print('Tests:', tests)
