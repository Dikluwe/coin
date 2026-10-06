import hashlib
import json
import pathlib
import platform
import re
import shutil
import subprocess

ROOT = pathlib.Path('/tmp/coin-render-first-frame')
SCRATCH = pathlib.Path('/tmp/coin-bounding-box-validation')
OUT = ROOT / 'docs/validation/bounding-box-linux'
OUT.mkdir(parents=True, exist_ok=True)
VARIANTS = ['wgpu-vulkan', 'bgfx-vulkan', 'bgfx-opengl']
BASE = 'bebbd1852c2718346f2270fbd3f39dc5d7f29c51'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive_log(path):
    archived = OUT / path.name
    archived.write_text('\n'.join(line.rstrip() for line in path.read_text().splitlines()).rstrip() + '\n')
    return {'log': path.name, 'raw_log_sha256': sha(path), 'log_sha256': sha(archived)}


def rgb_metrics(lines):
    return {
        'comparisons': len(lines),
        'max_rgb_error': max((int(re.search(r' max=(\d+)', line)[1]) for line in lines), default=0),
        'max_roi_mae': max((float(re.search(r'roi_mae=([^ ]+)', line)[1]) for line in lines), default=0),
        'pixels_over3': sum(int(re.search(r'pixels_over3=(\d+)', line)[1]) for line in lines),
    }


records = []
for variant in VARIANTS:
    phases = ['gpu', 'uv', 'fragment', 'screen', 'marker', 'shadow', 'core', 'depth']
    if variant != 'bgfx-opengl':
        phases.append('capture')
    if variant == 'bgfx-opengl':
        phases.append('drawstyle')
    for phase in phases:
        record = json.loads((SCRATCH / f'{phase}-{variant}.json').read_text())
        assert record['exit_code'] == 0, record
        log = SCRATCH / record['log']
        content = log.read_text()
        assert not re.search(r'(?im)^.*\b(skipped|skip:)\b', content), record
        record.update(archive_log(log))
        if phase == 'gpu':
            record['bounding_box_metrics'] = {kind: rgb_metrics([
                line for line in content.splitlines() if f'/{kind} oracle=CoinGL ' in line
            ]) for kind in ['GPU', 'CPU']}
            count = int(re.search(r'passed native_gl_comparisons=(\d+)', content)[1])
            assert record['bounding_box_metrics']['GPU']['comparisons'] == count
            assert all(m['max_roi_mae'] <= 1 for m in record['bounding_box_metrics'].values())
            record['native_gpu_comparisons'] = count
            record['original_caster_controls'] = re.findall(r'main_triangles=(\d+) original_caster_triangles=(\d+)', content)
            assert all(int(main) == 12 and int(caster) > 12 for main, caster in record['original_caster_controls'])
        elif phase == 'uv':
            record['uv_gpu_metrics'] = {oracle: rgb_metrics([
                line for line in content.splitlines() if f'/GPU oracle={oracle} ' in line
            ]) for oracle in ['CoinGL', 'CoinGL-equivalent-unit-zero-projection',
                              'CoinGL-equivalent-one-layer-source-over', 'independent-eight-stage-analytic']}
            assert [m['comparisons'] for m in record['uv_gpu_metrics'].values()] == [138, 4, 5, 50]
        elif phase == 'fragment':
            record['fragment_metrics'] = {kind: rgb_metrics([
                line for line in content.splitlines() if f'/{kind} gl_pixels=' in line
            ]) for kind in ['GPU', 'CPU']}
            assert record['fragment_metrics']['GPU']['comparisons'] == 116
        elif phase == 'screen':
            record['gpu_comparisons'] = sum('/GPU-CoinGL effect_pixels=' in line for line in content.splitlines())
            assert record['gpu_comparisons'] == 93
        elif phase == 'marker':
            lines = [line for line in content.splitlines() if '/GPU native_pixels=' in line]
            record['gpu_comparisons'] = len(lines)
            record['max_rgb_error'] = max(int(re.search(r'rgb_max=(\d+)', line)[1]) for line in lines)
            assert record['gpu_comparisons'] == 121
        elif phase in ['core', 'depth']:
            record['ctest_passed'] = int(re.search(r'0 tests failed out of (\d+)', content)[1])
            if phase == 'core' and variant == 'bgfx-opengl':
                record['per_test_environment_override'] = 'CMake pins bare DrawStyle to Vulkan; direct drawstyle-bgfx-opengl separately qualifies OpenGL.'
            if phase == 'depth':
                record['reason'] = 'Dedicated confirmation of the test-owned FramePlanBuilder copy after rebuilding all final targets.'
        records.append(record)

pilots = []
for stem, finding in [
    ('pilot-degenerate-capture-wgpu-vulkan', 'Original polygon-style preparation discarded zero-area object contours, omitting visible degenerate box lines. Native probes confirmed 41 line pixels and 25 point pixels. Boxes now preserve their repeated contour vertices.'),
    ('pilot-fill-raster-wgpu-vulkan', 'Bounds selected for line/point centers put filled edges on ambiguous CPU/native coverage centers. Filled controls now use the original unambiguous bounds; line/point center controls and RGB tolerances remain unchanged.'),
    ('pilot-coplanar-cpu-wgpu-vulkan', 'GPU/native IndexedMarkerSet planar PHONG box passed, but CPU LEQUAL selected different coincident faces because barycentric sums perturbed constant depth. Anchored affine depth interpolation fixed the CPU executor.'),
    ('pilot-clipped-corners-wgpu-vulkan', 'Native GL_QUADS POINTS retains boundary starts created when its implicit triangulation is clipped, including one central point. Ring-only clipping omitted that point and corner contributions. Boundary flags with packed diffuse RGBA restore native coverage and blending; ordinary polygon styles retain their existing ring behavior.'),
]:
    path = SCRATCH / (stem + '.json')
    if not path.exists():
        continue
    record = json.loads(path.read_text())
    assert record['exit_code'] != 0, record
    record['original_log_name'] = record['log']
    record.update(archive_log(SCRATCH / (stem + '.log')))
    record['finding'] = finding
    pilots.append(record)

build_logs = {name: archive_log(SCRATCH / name) for name in [
    'build-wgpu-final.log', 'build-bgfx-final.log', 'build-wgpu-depth.log', 'build-bgfx-depth.log',
    'build-wgpu-bbox-final.log', 'build-bgfx-bbox-final.log', 'native-degenerate-probe.log', 'native-clip-probe.log',
]}
for name in ['run.py', 'archive.py', 'native-degenerate-probe.cpp', 'native-clip-probe.cpp']:
    shutil.copyfile(SCRATCH / name, OUT / name)
sources = set(subprocess.check_output(['git', 'diff', BASE, '--name-only'], cwd=ROOT, text=True).splitlines())
sources.update(['docs/coin-render-bounding-box-contract.md',
                'src/rendering/coinrender/CoinRenderBoundingBoxCore.h',
                'testsuite/coinrender/CoinRenderBoundingBoxTest.cpp',
                'scripts/coinrender/run_animation_benchmark.py'])
sources = sorted(path for path in sources if not path.startswith('docs/validation/'))
unchanged_gpu_paths = ['src/rendering/coinwgpu', 'src/rendering/coinbgfx', 'src/rendering/coinrender/CoinRenderFramePlan.h']
subprocess.run(['git', 'diff', '--exit-code', BASE, '--', *unchanged_gpu_paths], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
binaries = {}
for name in ['wgpu', 'bgfx']:
    build = pathlib.Path('/tmp/coin-render-first-frame-' + name)
    paths = ['lib/libCoin.so', 'lib/libCoinRender.so', 'bin/CoinRenderBoundingBoxTest',
             'bin/CoinRenderProjectiveUvTest', 'bin/CoinRenderFragmentPolicyTest',
             'bin/CoinRenderScreenContentTest', 'bin/CoinRenderMarkerSetTest',
             'bin/CoinRenderShadowReferenceTest', 'bin/CoinRenderDepthContractTest',
             'bin/CoinRenderDrawStyleTest']
    binaries[name] = {str(build / path): sha(build / path) for path in paths}
totals = {
    'bounding_box_native_gpu_comparisons': sum(r.get('native_gpu_comparisons', 0) for r in records),
    'bounding_box_native_cpu_comparisons': sum(r.get('bounding_box_metrics', {}).get('CPU', {}).get('comparisons', 0) for r in records),
    'uv_native_gpu_comparisons': sum(r.get('uv_gpu_metrics', {}).get('CoinGL', {}).get('comparisons', 0) for r in records),
    'uv_equivalent_gpu_comparisons': sum(r.get('uv_gpu_metrics', {}).get(o, {}).get('comparisons', 0) for r in records for o in ['CoinGL-equivalent-unit-zero-projection', 'CoinGL-equivalent-one-layer-source-over']),
    'uv_analytic_gpu_comparisons': sum(r.get('uv_gpu_metrics', {}).get('independent-eight-stage-analytic', {}).get('comparisons', 0) for r in records),
    'fragment_gpu_comparisons': sum(r.get('fragment_metrics', {}).get('GPU', {}).get('comparisons', 0) for r in records),
    'screen_gpu_comparisons': sum(r.get('gpu_comparisons', 0) for r in records if r['phase'] == 'screen'),
    'marker_gpu_comparisons': sum(r.get('gpu_comparisons', 0) for r in records if r['phase'] == 'marker'),
    'core_ctest_passed': sum(r.get('ctest_passed', 0) for r in records if r['phase'] == 'core'),
    'depth_contract_replays': sum(r.get('ctest_passed', 0) for r in records if r['phase'] == 'depth'),
    'capture_executions': sum(r['phase'] == 'capture' for r in records),
    'native_shadow_reference_executions': sum(r['phase'] == 'shadow' for r in records),
    'direct_drawstyle_opengl_executions': sum(r['phase'] == 'drawstyle' for r in records),
    'final_skips': 0,
}
assert totals['bounding_box_native_gpu_comparisons'] == 411
assert totals['bounding_box_native_cpu_comparisons'] == 402
assert totals['core_ctest_passed'] == 58
assert totals['depth_contract_replays'] == 3
summary = {
    'date': '2026-10-06', 'campaign_started': '2026-10-05', 'base_commit': BASE,
    'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
    'status': 'passed',
    'machine': {'platform': platform.platform(), 'gpu_driver': 'NVIDIA GeForce RTX 3060 Laptop GPU, 610.57.04'},
    'profile': {
        'reference': 'Native Coin/OpenGL for BOUNDING_BOX; prior UV gate keeps its separately labeled equivalent and analytic oracles.',
        'variants': VARIANTS, 'pixel_comparison': 'RGB; MAE <=1 and >3 outliers <=max(4,2% active ROI). Framebuffer alpha unqualified.',
        'execution': 'GPU test processes serialized; builds use parallelism 6.',
        'ffi_protocol': 45, 'gpu_and_vertex_layout_paths_unchanged': unchanged_gpu_paths,
        'rust': 'Not rerun: Rust, shaders and FFI paths are unchanged from the prior projective UV campaign, whose 38 tests passed.',
        'log_normalization': 'Trailing whitespace and empty EOF lines removed; raw and archived SHA256 recorded.',
    },
    'totals': totals, 'source_sha256': {path: sha(ROOT / path) for path in sources},
    'binaries_sha256': binaries, 'runs': records, 'pilots': pilots, 'build_logs': build_logs,
    'artifacts_sha256': {path.name: sha(path) for path in sorted(OUT.iterdir()) if path.is_file() and path.name != 'summary.json'},
    'limits': [
        'Linux/NVIDIA only; other platforms and drivers require their own gates. No performance benchmark.',
        'No new general UV functions/texgen, higher texture units, filters/formats or framebuffer alpha qualification.',
        'Screen/marker box shapes within active shadow groups with maps still require unsupported original raster caster capture.',
        'Finite degenerate boxes admitted; empty boxes emit no geometry. Nonfinite bounds/center/extent rejected.',
        'Polygon offset factor with zero projected face area remains explicitly unsupported.',
        'Custom GLRender overrides that bypass shouldGLRender need their own adapter.',
    ],
}
(OUT / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n')
print(json.dumps(totals, indent=2))
