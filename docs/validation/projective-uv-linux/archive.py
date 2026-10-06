import hashlib
import json
import pathlib
import platform
import re
import shutil
import subprocess

ROOT = pathlib.Path('/tmp/coin-render-first-frame')
SCRATCH = pathlib.Path('/tmp/coin-projective-uv-validation')
OUT = ROOT / 'docs/validation/projective-uv-linux'
OUT.mkdir(parents=True, exist_ok=True)
VARIANTS = ['wgpu-vulkan', 'bgfx-vulkan', 'bgfx-opengl']
BASE = 'f4cebd50b5edf9df3f0158090febe35c6128b617'


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
    phases = ['gpu', 'fragment', 'screen', 'marker', 'shadow', 'core']
    if variant != 'bgfx-opengl':
        phases.append('capture')
    if variant == 'wgpu-vulkan':
        phases.append('multidevice')
    if variant == 'bgfx-opengl':
        phases.append('drawstyle')
    for phase in phases:
        record = json.loads((SCRATCH / f'{phase}-{variant}.json').read_text())
        assert record['exit_code'] == 0, record
        log = SCRATCH / record['log']
        content = log.read_text()
        assert not re.search(r'(?im)^.*\b(skipped|skip:)\b', content), record
        record.update(archive_log(log))
        if phase in ['gpu', 'capture']:
            metrics = {}
            for oracle in ['CoinGL', 'CoinGL-equivalent-unit-zero-projection',
                           'CoinGL-equivalent-one-layer-source-over', 'independent-eight-stage-analytic']:
                metrics[oracle] = {kind: rgb_metrics([
                    line for line in content.splitlines()
                    if f'/{kind} oracle={oracle} ' in line
                ]) for kind in ['CPU', 'GPU']}
            record['projective_metrics'] = metrics
            assert all(value['max_roi_mae'] <= 1 for oracle in metrics.values() for value in oracle.values()), metrics
            final = re.search(r'passed native_gl_comparisons=(\d+) equivalent_gl_projection_comparisons=(\d+) '
                              r'equivalent_one_layer_gl_comparisons=(\d+) '
                              r'independent_analytic_cpu_comparisons=(\d+) independent_analytic_gpu_comparisons=(\d+)', content)
            assert final, record
            record['reported_counts'] = dict(zip(['native_gl', 'equivalent_gl_projection', 'equivalent_one_layer_gl',
                                                  'analytic_cpu', 'analytic_gpu'], map(int, final.groups())))
            assert record['reported_counts']['analytic_cpu'] == 50
            assert record['reported_counts']['analytic_gpu'] == (50 if phase == 'gpu' else 0)
            for key, oracle, kind in [('native_gl', 'CoinGL', 'GPU'),
                                      ('equivalent_gl_projection', 'CoinGL-equivalent-unit-zero-projection', 'GPU'),
                                      ('equivalent_one_layer_gl', 'CoinGL-equivalent-one-layer-source-over', 'GPU'),
                                      ('analytic_cpu', 'independent-eight-stage-analytic', 'CPU'),
                                      ('analytic_gpu', 'independent-eight-stage-analytic', 'GPU')]:
                assert record['reported_counts'][key] == metrics[oracle][kind]['comparisons']
        elif phase == 'fragment':
            record['fragment_metrics'] = {kind: rgb_metrics([
                line for line in content.splitlines() if f'/{kind} gl_pixels=' in line
            ]) for kind in ['GPU', 'CPU']}
            assert record['fragment_metrics']['GPU']['comparisons'] == 116
            assert record['fragment_metrics']['CPU']['comparisons'] == 103
        elif phase == 'screen':
            record['gpu_comparisons'] = sum('/GPU-CoinGL effect_pixels=' in line for line in content.splitlines())
            assert record['gpu_comparisons'] == 93
        elif phase == 'marker':
            lines = [line for line in content.splitlines() if '/GPU native_pixels=' in line]
            record['gpu_comparisons'] = len(lines)
            record['max_rgb_error'] = max(int(re.search(r'rgb_max=(\d+)', line)[1]) for line in lines)
            assert record['gpu_comparisons'] == 121
        elif phase == 'core':
            record['ctest_passed'] = int(re.search(r'0 tests failed out of (\d+)', content)[1])
            if variant == 'bgfx-opengl':
                record['per_test_environment_override'] = (
                    'CMake pins bare CoinRenderDrawStyleTest to BGFX/Vulkan. '
                    'The direct drawstyle-bgfx-opengl run separately qualifies OpenGL.')
        records.append(record)

pilots = []
for stem, finding in [
    ('pilot-sparse-seven-bgfx-vulkan', 'The native fixed-function context has four image units. Unit seven is now compared with an explicitly labeled equivalent unit-zero scene; eight simultaneous units use an independent analytic oracle.'),
    ('pilot-line-raster-bgfx-vulkan', 'An integer-position endpoint made line coverage ambiguous. The fixture now places endpoints at pixel centers; sampling tolerances are unchanged.'),
    ('pilot-shadow-cpu-admission-wgpu-vulkan', 'The CPU reference cannot execute active ShadowGroup. Its sampling gate now uses the real GPU executor and mandatory native CoinGL.'),
    ('pilot-point-raster-wgpu-vulkan', 'Four five-pixel points on pixel boundaries yielded CPU99 versus native100. Geometry was moved to pixel centers; point size, UV and tolerances are unchanged.'),
    ('pilot-native-shadow-shader-wgpu-vulkan', 'An ordinary directional light exposed duplicate DirectionalLight definitions in the native generated shader. The control uses a white emissive material instead.'),
    ('pilot-shadow-capability-wgpu-vulkan', 'A zero-map ShadowGroup is outside the executable action profile, which requires one to eight visible passes. The final fixture uses a real admitted map.'),
    ('pilot-rtt-sampling-wgpu-vulkan', 'The native FBO producer inherited the consumer texture matrix, composing it with its own. The pbuffer path and current capture start independently. The final gate visits SceneTexture before the consumer matrix; inherited producer texture state remains unqualified.'),
    ('pilot-peeling-native-arb-wgpu-vulkan', 'Native sorted layers uses a legacy ARB program that samples texture zero with TEX, ignores texture environment and adds sampled RGB to primary color. White material saturated the reference. The classic projective one-layer gate uses an explicitly labeled equivalent source-over reference; native shadow-generated shader gates remain native sorted-layer comparisons.'),
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

extra_logs = {name: archive_log(SCRATCH / name) for name in [
    'build-wgpu.log', 'build-bgfx.log', 'build-wgpu-policy.log', 'build-bgfx-policy.log',
    'build-wgpu-uv-final.log', 'build-bgfx-uv-final.log', 'rust-tests-final.log',
]}
rust_content = (SCRATCH / 'rust-tests-final.log').read_text()
rust_results = re.findall(r'test result: ok\. (\d+) passed; 0 failed; 0 ignored;', rust_content)
assert list(map(int, rust_results)) == [33, 2, 3], rust_results
for name in ['run.py', 'archive.py', 'abi-probe.cpp', 'abi-layout.json']:
    shutil.copyfile(SCRATCH / name, OUT / name)

sources = set(subprocess.check_output(['git', 'diff', BASE, '--name-only'], cwd=ROOT, text=True).splitlines())
sources.update(['docs/coin-render-projective-uv-contract.md',
                'src/rendering/coinrender/CoinRenderTextureCoordinateCore.h',
                'testsuite/coinrender/CoinRenderProjectiveUvTest.cpp',
                'scripts/coinrender/run_animation_benchmark.py'])
sources = sorted(path for path in sources if not path.startswith('docs/validation/'))
binaries = {}
for name in ['wgpu', 'bgfx']:
    build = pathlib.Path('/tmp/coin-render-first-frame-' + name)
    paths = ['lib/libCoin.so', 'lib/libCoinRender.so', 'bin/CoinRenderProjectiveUvTest',
             'bin/CoinRenderFragmentPolicyTest', 'bin/CoinRenderScreenContentTest',
             'bin/CoinRenderMarkerSetTest', 'bin/CoinRenderShadowReferenceTest',
             'bin/CoinRenderFrameCoreTest', 'bin/CoinRenderPlanAssemblyCoreTest',
             'bin/CoinRenderDrawStyleTest', 'bin/CoinBgfxCoreTest']
    if name == 'wgpu':
        paths += ['bin/CoinWgpuFfiFrameTest', 'bin/CoinWgpuMultiDeviceTest']
    binaries[name] = {str(build / path): sha(build / path) for path in paths}

gpu_records = [r for r in records if r['phase'] == 'gpu']
totals = {
    'projective_native_gl_gpu_comparisons': sum(r['reported_counts']['native_gl'] for r in gpu_records),
    'projective_equivalent_gl_projection_gpu_comparisons': sum(r['reported_counts']['equivalent_gl_projection'] for r in gpu_records),
    'projective_equivalent_one_layer_gl_gpu_comparisons': sum(r['reported_counts']['equivalent_one_layer_gl'] for r in gpu_records),
    'projective_analytic_gpu_comparisons': sum(r['reported_counts']['analytic_gpu'] for r in gpu_records),
    'projective_analytic_cpu_comparisons_in_gpu_runs': sum(r['reported_counts']['analytic_cpu'] for r in gpu_records),
    'fragment_gpu_comparisons': sum(r.get('fragment_metrics', {}).get('GPU', {}).get('comparisons', 0) for r in records),
    'screen_gpu_comparisons': sum(r.get('gpu_comparisons', 0) for r in records if r['phase'] == 'screen'),
    'marker_gpu_comparisons': sum(r.get('gpu_comparisons', 0) for r in records if r['phase'] == 'marker'),
    'core_ctest_passed': sum(r.get('ctest_passed', 0) for r in records),
    'rust_tests_passed': sum(map(int, rust_results)),
    'capture_executions': sum(r['phase'] == 'capture' for r in records),
    'native_shadow_reference_executions': sum(r['phase'] == 'shadow' for r in records),
    'direct_drawstyle_opengl_executions': sum(r['phase'] == 'drawstyle' for r in records),
    'wgpu_multidevice_executions': sum(r['phase'] == 'multidevice' for r in records),
    'final_skips': 0,
}
assert totals['projective_analytic_gpu_comparisons'] == 150
assert totals['fragment_gpu_comparisons'] == 348
assert totals['screen_gpu_comparisons'] == 279
assert totals['marker_gpu_comparisons'] == 363
assert totals['core_ctest_passed'] == 58
summary = {
    'date': '2026-10-05',
    'base_commit': BASE,
    'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
    'status': 'passed',
    'machine': {'platform': platform.platform(), 'gpu_driver': 'NVIDIA GeForce RTX 3060 Laptop GPU, 610.57.04'},
    'profile': {
        'reference': 'Native Coin/OpenGL plus explicitly distinct equivalent-unit-zero, equivalent-one-layer source-over and independent-eight-stage analytic oracles.',
        'variants': VARIANTS,
        'pixel_comparison': 'RGB; framebuffer alpha is not qualified. UV gate MAE <=1 and >3 outliers <=max(4,2% of active ROI).',
        'execution': 'GPU test processes serialized; builds use parallelism six.',
        'ffi_protocol': 45,
        'abi_layout': json.loads((SCRATCH / 'abi-layout.json').read_text()),
        'log_normalization': 'Trailing whitespace and empty EOF lines stripped; raw and archived SHA256 recorded for each log.',
    },
    'totals': totals,
    'source_sha256': {path: sha(ROOT / path) for path in sources},
    'binaries_sha256': binaries,
    'runs': records,
    'pilots': pilots,
    'build_and_rust_logs': extra_logs,
    'artifacts_sha256': {path.name: sha(path) for path in sorted(OUT.iterdir()) if path.is_file() and path.name != 'summary.json'},
    'limits': [
        'Linux/NVIDIA qualification; Windows and other GPU drivers need their own gate execution.',
        'Native fixed-function GL_MAX_TEXTURE_UNITS=4. Eight simultaneous texture stages are qualified analytically, not against an eight-stage native fixed-function image.',
        'Shadow texture policy follows the generated native shader: transformed ST without Q division. Shader program activation exceptions remain explicitly unsupported.',
        'Shadow, OIT and RTT sampling gates execute actual GPU targets; capture-only mode does not claim these executions.',
        'No general framebuffer alpha, RTT filtering or shadow-alpha-caster equivalence claim.',
        'External texture-matrix inheritance in RTT producers is not qualified: native FBO inherits it while native pbuffer and current capture start independently.',
        'Complete vertices grow by 64 bytes common/wgpu and 32 bytes BGFX; BGFX compact and instanced layouts are unchanged. No performance benchmark was run.',
    ],
}
(OUT / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n')
print(json.dumps(totals, indent=2))
