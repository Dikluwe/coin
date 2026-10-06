import hashlib
import json
import pathlib
import platform
import re
import shutil
import subprocess

ROOT = pathlib.Path('/tmp/coin-render-first-frame')
SCRATCH = pathlib.Path('/tmp/coin-rgb-replace-validation')
OUT = ROOT / 'docs/validation/rgb-replace-linux'
OUT.mkdir(parents=True, exist_ok=True)
VARIANTS = ['wgpu-vulkan', 'bgfx-vulkan', 'bgfx-opengl']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def archive_log(path):
    # Only remove trailing whitespace for Git; keep the raw hash as provenance.
    content = '\n'.join(line.rstrip() for line in path.read_text().splitlines()) + '\n'
    archived = OUT / path.name
    archived.write_text(content)
    return {'log': path.name, 'raw_log_sha256': sha(path), 'log_sha256': sha(archived)}

records = []
for variant in VARIANTS:
    phases = ['gpu', 'screen', 'marker', 'regression']
    if variant != 'bgfx-opengl':
        phases.append('capture')
    for phase in phases:
        record = json.loads((SCRATCH / f'{phase}-{variant}.json').read_text())
        assert record['exit_code'] == 0, record
        log = SCRATCH / record['log']
        text = log.read_text()
        assert not re.search(r'(?im)^.*\b(skipped|skip:)\b', text), record
        record.update(archive_log(log))
        if phase == 'gpu':
            metrics = {}
            for kind in ['GPU', 'CPU']:
                lines = [line for line in text.splitlines() if f'/{kind} gl_pixels=' in line]
                metrics[kind] = {
                    'comparisons': len(lines),
                    'max_rgb_error': max(int(re.search(r' max=(\d+)', line)[1]) for line in lines),
                    'max_roi_mae': max(float(re.search(r'roi_mae=([\d.]+)', line)[1]) for line in lines),
                    'pixels_over3': sum(int(re.search(r'pixels_over3=(\d+)', line)[1]) for line in lines),
                }
            assert metrics['GPU']['comparisons'] == 116
            assert metrics['CPU']['comparisons'] == 103
            assert metrics['GPU']['max_rgb_error'] <= 1
            record['fragment_metrics'] = metrics
            record['new_replace_gpu_cases'] = 6
            record['gpu_only_cases'] = 13  # Weighted OIT and temporary RTT producers.
        elif phase == 'screen':
            record['gpu_comparisons'] = sum('/GPU-CoinGL effect_pixels=' in line for line in text.splitlines())
        elif phase == 'marker':
            lines = [line for line in text.splitlines() if '/GPU native_pixels=' in line]
            record['gpu_comparisons'] = len(lines)
            record['max_rgb_error'] = max(int(re.search(r'rgb_max=(\d+)', line)[1]) for line in lines)
        elif phase == 'regression':
            record['ctest_passed'] = int(re.search(r'0 tests failed out of (\d+)', text)[1])
        records.append(record)

pilot = json.loads((SCRATCH / 'pilot-unused-slot-wgpu-vulkan.json').read_text())
pilot['original_log_name'] = pilot['log']
pilot.update(archive_log(SCRATCH / 'pilot-unused-slot-wgpu-vulkan.log'))
assert pilot['exit_code'] == 1
pilot['finding'] = ('Before the fix, RGB REPLACE with OVERALL slot zero opaque and an unused '
                    'transparent material slot rendered immediately instead of deferred: '
                    'CPU green versus CoinGL red, 3584 pixels, ROI MAE 170, maximum error 255.')
extras = {}
for name in ['build-pilot-wgpu.log', 'build-wgpu.log', 'build-bgfx.log']:
    extras[name] = archive_log(SCRATCH / name)
for name in ['run.py', 'archive.py']:
    shutil.copyfile(SCRATCH / name, OUT / name)

sources = [
    'src/rendering/coinrender/CoinRenderComposition.h',
    'src/rendering/coinrender/CoinRenderTextureCombineCore.h',
    'src/rendering/coinrender/CoinRenderStateCore.h',
    'src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp',
    'src/rendering/coinrender/CoinRenderScreenRasterCore.h',
    'src/actions/CoinRenderAction.cpp',
    'src/nodes/SoMaterial.cpp',
    'src/shapenodes/SoShape.cpp',
    'testsuite/coinrender/CoinRenderFragmentPolicyTest.cpp',
    'testsuite/coinrender/CoinRenderScreenContentTest.cpp',
    'testsuite/coinrender/CoinRenderMarkerSetTest.cpp',
    'scripts/coinrender/run_animation_benchmark.py',
    'docs/coin-render-fragment-policy-contract.md',
    'docs/coin-render-parity-gaps-windows-20261005.md',
]
binaries = {}
for name in ['wgpu', 'bgfx']:
    build = pathlib.Path('/tmp/coin-render-first-frame-' + name)
    paths = ['lib/libCoin.so', 'lib/libCoinRender.so', 'bin/CoinRenderFragmentPolicyTest',
             'bin/CoinRenderScreenContentTest', 'bin/CoinRenderMarkerSetTest']
    binaries[name] = {str(build / path): sha(build / path) for path in paths}
totals = {
    'fragment_gpu_comparisons': sum(r.get('fragment_metrics', {}).get('GPU', {}).get('comparisons', 0) for r in records),
    'fragment_cpu_comparisons': sum(r.get('fragment_metrics', {}).get('CPU', {}).get('comparisons', 0) for r in records),
    'fragment_gpu_max_rgb_error': max(r.get('fragment_metrics', {}).get('GPU', {}).get('max_rgb_error', 0) for r in records),
    'new_replace_gpu_cases': sum(r.get('new_replace_gpu_cases', 0) for r in records),
    'screen_gpu_comparisons': sum(r.get('gpu_comparisons', 0) for r in records if r['phase'] == 'screen'),
    'marker_gpu_comparisons': sum(r.get('gpu_comparisons', 0) for r in records if r['phase'] == 'marker'),
    'ctest_passed': sum(r.get('ctest_passed', 0) for r in records),
    'capture_executions': sum(r['phase'] == 'capture' for r in records),
    'final_skips': 0,
}
assert totals['fragment_gpu_comparisons'] == 348
assert totals['screen_gpu_comparisons'] == 279
assert totals['marker_gpu_comparisons'] == 363
assert totals['ctest_passed'] == 49
summary = {
    'date': '2026-10-05',
    'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
    'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
    'status': 'passed',
    'machine': {
        'platform': platform.platform(),
        'gpu_driver': 'NVIDIA GeForce RTX 3060 Laptop GPU, 610.57.04',
        'gpu_driver_source': 'Same machine as docs/validation/marker-linux/summary.json; verified again with nvidia-smi.',
    },
    'profile': {
        'reference': 'Coin/OpenGL',
        'variants': VARIANTS,
        'pixel_comparison': 'RGB; framebuffer alpha is not qualified.',
        'execution': 'GPU test processes serialized; wgpu and BGFX builds serialized with parallelism 6.',
        'changes': 'Common composition classification only; no ABI, Rust, texture payload or shader changes.',
        'log_normalization': 'Trailing whitespace stripped; raw and archived SHA256 recorded for each log.',
    },
    'totals': totals,
    'source_sha256': {path: sha(ROOT / path) for path in sources},
    'binaries_sha256': binaries,
    'runs': records,
    'pilot': pilot,
    'build_logs': extras,
    'artifacts_sha256': {path.name: sha(path) for path in sorted(OUT.iterdir()) if path.is_file() and path.name != 'summary.json'},
    'limits': [
        'Linux/NVIDIA qualification; Windows and other GPU drivers need their own gate execution.',
        'Authored TextureCombine gates use combine before active image; later traversal changes are not qualified.',
        'No general framebuffer alpha, RTT filtering or shadow alpha caster equivalence claim.',
        'Capture-only modes do not claim native GPU, OIT or temporary RTT producer execution.',
        'No performance benchmark was run for this semantic correction.',
    ],
}
(OUT / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n')
print(json.dumps(totals, indent=2))
