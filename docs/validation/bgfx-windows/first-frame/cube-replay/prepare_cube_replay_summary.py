from pathlib import Path
root = Path('H:/Git/coin/build')
script = (root / 'summarize_common_optimizations.py').read_text()
script = script.replace('common-optimizations', 'cube-replay').replace('common-opt', 'cube-replay')
script = script.replace('3633e6a5eb', '113ee2b4f8')
script = script.replace("(?:WebGPU|BGFX-\\S+)", "(?:CoinGL|WebGPU|BGFX-\\S+)")
start = script.index('tests = {}')
stop = script.index('hashes = {}')
script = script[:start] + '''tests = {}
names = ['cube-replay-core-tests'] + [f'cube-replay-{f}-{a}-tests' for f, apis in
    [('wgpu', ('vulkan', 'gl', 'dx12')), ('bgfx', ('vulkan', 'opengl', 'd3d12'))] for a in apis]
for name in names:
    xml = ET.parse(root / f'build/{name}.xml').getroot()
    cases = {case.attrib['name']: 'failed' if case.find('failure') is not None else
             'skipped' if case.find('skipped') is not None else 'passed' for case in xml.iter('testcase')}
    tests[name] = {'total': len(cases), 'passed': sum(v == 'passed' for v in cases.values()),
                  'failed': sum(v == 'failed' for v in cases.values()),
                  'skipped': sum(v == 'skipped' for v in cases.values()), 'cases': cases}
    assert tests[name]['failed'] == 0 and tests[name]['skipped'] == 0
    archive(root / f'build/{name}.log')
    archive(root / f'build/{name}.xml')

''' + script[stop:]
start = script.index("for name in ('measure_common_optimizations.ps1'")
stop = script.index('summary = {', start)
script = script[:start] + '''for name in ('measure_cube_replay.ps1', 'qualify_cube_replay.ps1',
             'summarize_cube_replay.py', 'prepare_cube_replay_summary.py',
             'cube-replay-wgpu-tests.txt', 'cube-replay-bgfx-tests.txt',
             'cube-replay-wgpu-build.log', 'cube-replay-bgfx-build.log', 'cube-replay-action-build.log',
             'install_cube_replay.ps1', 'cube-replay-wgpu-install.log', 'cube-replay-bgfx-install.log',
             'cube-replay-install-verification.json'):
    archive(root / 'build' / name)

native = [r for r in runs if r['flavor'] == 'coin']
assert len(native) == 3 and len(set(r['rgba_fnv64'] for r in native)) == 1
native_summary = {'first_median_ms': statistics.median(r['first_frame_ms'] for r in native),
    'first_range_ms': [min(r['first_frame_ms'] for r in native), max(r['first_frame_ms'] for r in native)],
    'warm_median_ms': statistics.median(r['warm_median_ms'] for r in native),
    'image_sha256': native[0]['image_sha256'], 'rgba_fnv64': native[0]['rgba_fnv64']}
for comparison in comparisons:
    comparison['remaining_native_gap_ms'] = comparison['after']['first_median_ms'] - native_summary['first_median_ms']

''' + script[stop:]
script = script.replace('Shared CoinRender float validation, enabled-unit handling, composition reuse and native untextured Cube capture.',
    'Bounded per-frame geometry template replay for exact native untextured OVERALL Cubes, with fresh occurrence state.')
script = script.replace("'comparisons': comparisons, 'update_controls': updates, 'tests': tests,",
    "'native_coin_opengl': native_summary, 'comparisons': comparisons, 'update_controls': updates, 'tests': tests,")
script = script.replace("print('Updates:', updates)", "print('Updates:', updates)\nprint('Coin OpenGL:', native_summary)")
(root / 'summarize_cube_replay.py').write_text(script, encoding='utf-8')
installation = (root / 'install_common_optimizations.ps1').read_text().replace('common-opt', 'cube-replay')
(root / 'install_cube_replay.ps1').write_text(installation, encoding='utf-8')
