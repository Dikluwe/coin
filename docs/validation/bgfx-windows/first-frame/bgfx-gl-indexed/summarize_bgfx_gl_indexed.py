import hashlib
import json
from pathlib import Path
import re
import statistics
import xml.etree.ElementTree as ET

root = Path('H:/Git/coin')
build = root / 'build'
runs = json.loads((build / 'bgfx-gl-indexed-runs.json').read_text(encoding='utf-8-sig'))

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def read(run):
    data = (root / run['log']).read_text(encoding='utf-8-sig', errors='replace')
    result = dict(run)
    assert run['exit_code'] == 0
    result['first_ms'] = float(re.search(r'_first_frame_ms=([\d.]+)', data)[1])
    result['warm_ms'] = float(re.search(r'^BGFX-[^\s]+ frames=\d+ median_ms=([\d.]+)', data, re.M)[1])
    result['rgba_fnv64'] = re.search(r'rgba_fnv64=(0x[\da-f]+)', data)[1]
    result['image_sha256'] = digest(root / run['image'])
    phase = re.search(r'^COIN_RENDER_PHASE bgfx lower_ms=(.*)', data, re.M)
    if phase:
        result['first_backend_trace'] = {k: v for k, v in re.findall(r'(\w+)=([^\s]+)', 'lower_ms=' + phase[1])}
    return result

all_runs = [read(run) for run in runs]
comparisons = []
image_checks = []
for api in ('opengl', 'vulkan', 'd3d12'):
    grouped = {}
    for variant in ('before', 'after'):
        samples = [r for r in all_runs if r['api'] == api and r['variant'] == variant and r['mode'] == 'static' and isinstance(r['sample'], int)]
        assert len(samples) == 3
        assert all(r['warm_ms'] < 30 for r in samples), 'Timed run exceeded the BGFX warm-frame stability gate'
        warm = next(r for r in all_runs if r['api'] == api and r['variant'] == variant and r['sample'] == 'warm')
        trace = next(r for r in all_runs if r['api'] == api and r['variant'] == variant and r['sample'] == 'trace')
        grouped[variant] = {'first_median_ms': statistics.median(r['first_ms'] for r in samples),
          'first_range_ms': [min(r['first_ms'] for r in samples), max(r['first_ms'] for r in samples)],
          'warm_control_ms': warm['warm_ms'], 'trace': trace['first_backend_trace']}
    before, after = grouped['before'], grouped['after']
    comparisons.append({'api': api, **grouped,
      'reduction_percent': 100 * (1 - after['first_median_ms'] / before['first_median_ms'])})
    for sample in (1, 2, 3, 'trace', 'warm'):
        pair = [next(r for r in all_runs if r['api'] == api and r['variant'] == v and r['sample'] == sample) for v in ('before', 'after')]
        assert pair[0]['image_sha256'] == pair[1]['image_sha256']
        assert pair[0]['rgba_fnv64'] == pair[1]['rgba_fnv64']
        image_checks.append({'api': api, 'sample': sample, 'sha256': pair[0]['image_sha256'], 'equal': True})

updates = []
for mode in ('camera', 'material'):
    samples = [r['sample'] for r in all_runs if r['mode'] == mode and r['variant'] == 'before']
    before, after = [], []
    for sample in samples:
        pair = [next(r for r in all_runs if r['mode'] == mode and r['variant'] == v and r['sample'] == sample) for v in ('before', 'after')]
        assert pair[0]['image_sha256'] == pair[1]['image_sha256'] and pair[0]['rgba_fnv64'] == pair[1]['rgba_fnv64']
        before.append(pair[0]); after.append(pair[1])
    updates.append({'mode': mode, 'sample_count': len(samples),
      'before_first_ms': statistics.median(r['first_ms'] for r in before), 'after_first_ms': statistics.median(r['first_ms'] for r in after),
      'before_warm_ms': statistics.median(r['warm_ms'] for r in before), 'after_warm_ms': statistics.median(r['warm_ms'] for r in after),
      'before_warm_range_ms': [min(r['warm_ms'] for r in before), max(r['warm_ms'] for r in before)],
      'after_warm_range_ms': [min(r['warm_ms'] for r in after), max(r['warm_ms'] for r in after)], 'images_equal': True})

test_results = {}
for name in ('bgfx-gl-indexed-core', 'bgfx-gl-indexed-scoped-clip', 'bgfx-gl-indexed-tests'):
    path = build / (name + '.xml')
    if not path.exists():
        continue
    tree = ET.parse(path)
    cases = tree.findall('.//testcase')
    test_results[name] = {'total': len(cases), 'failed': sum(c.find('failure') is not None for c in cases),
      'skipped': sum(c.find('skipped') is not None for c in cases)}
    assert not test_results[name]['failed'] and not test_results[name]['skipped']

binary_hashes = {v: {f: digest(build / directory / f) for f in ('Coin4.dll', 'CoinRender4.dll', 'coin_render_gl_benchmark.exe')}
  for v, directory in (('before', 'bgfx-gl-indexed-baseline'), ('after', 'coin-render-bgfx-msvc/bin'))}
assert binary_hashes['before']['Coin4.dll'] == binary_hashes['after']['Coin4.dll']
controls = []
control_manifest = build / 'bgfx-gl-indexed-controls.json'
if control_manifest.exists():
    control_runs = json.loads(control_manifest.read_text(encoding='utf-8-sig'))
    for backend, api in (('gl', 'opengl'), ('wgpu', 'gl'), ('wgpu', 'vulkan')):
        samples = [r for r in control_runs if r['backend'] == backend and r['api'] == api]
        assert len(samples) == 3 and all(r['exit_code'] == 0 for r in samples)
        first, warm = [], []
        for sample in samples:
            data = (root / sample['log']).read_text(encoding='utf-8-sig', errors='replace')
            first.append(float(re.search(r'_first_frame_ms=([\d.]+)', data)[1]))
            warm.append(float(re.search(r'^(?:CoinGL|WebGPU) frames=8 median_ms=([\d.]+)', data, re.M)[1]))
        controls.append({'backend': backend, 'api': api, 'first_median_ms': statistics.median(first),
          'first_range_ms': [min(first), max(first)], 'warm_median_ms': statistics.median(warm), 'runs': samples})
installation_path = build / 'bgfx-gl-indexed-install-verification.json'
installation = json.loads(installation_path.read_text(encoding='utf-8-sig')) if installation_path.exists() else []
summary = {'date': '2026-10-03', 'baseline_commit': '30f1f4018f', 'scene': 'city-40000.iv', 'size': [1024, 1024],
  'measurement': 'First render plus synchronous RGBA copy; serial fresh processes, driver/system caches retained; no tracing in timed medians.',
  'comparisons': comparisons, 'updates': updates, 'image_checks': image_checks,
  'tests': test_results, 'binary_hashes': binary_hashes, 'runs': all_runs,
  'controls': controls, 'installation': installation,
  'final_scope': 'Only draws eligible for the existing opaque PHONG batching guard, with source ranges up to 4096 vertices.',
  'discarded_wide_attempt': {'initial_tests_interrupted': True,
    'vulkan_sorted_layers': 'One access violation; passed recheck.',
    'opengl_weighted_oit': 'Timed out at 60 seconds; direct execution passed in 81.60s versus baseline 8.97s.',
    'opengl_sorted_layers': 'Timed out at 60 seconds.',
    'final_scoped_clip_tests': 'All three passed; OpenGL weighted OIT 4.81s, sorted layers 2.70s.'},
  'discarded_unstable_measurements': 'Complete 34-run scoped round preserved separately; baseline OpenGL first sample reached 28949.4ms and warm median 173.729ms. Final timing uses a separate complete round after observing idle GPU, with all timed warm medians below 30ms.'}
(build / 'bgfx-gl-indexed-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for row in comparisons:
    print(row['api'], 'first', row['before']['first_median_ms'], '->', row['after']['first_median_ms'],
          'gain%', round(row['reduction_percent'], 2), 'warm', row['before']['warm_control_ms'], '->', row['after']['warm_control_ms'])
print('Exact image pairs:', len(image_checks) + sum(row['sample_count'] for row in updates))
print('Updates:', updates)
print('Tests:', test_results)
print('Controls:', controls)
