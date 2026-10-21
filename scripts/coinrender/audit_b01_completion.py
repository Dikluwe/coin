#!/usr/bin/env python3
"""Audit B01 Linux evidence; native CoinGL failures remain failures."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile
import xml.etree.ElementTree as ET
from run_p20_physical_matrix import rgb_metrics, SCENES, BACKENDS

CELLS = {(b, g, a) for b in ('bgfx', 'wgpu') for g in ('amd', 'nvidia')
         for a in ('vulkan', 'opengl') if (b, g, a) != ('wgpu', 'nvidia', 'opengl')}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--source-snapshot', type=Path, help='Verify archived B01 sources when the checkout has advanced')
    args = parser.parse_args()
    root = args.evidence
    repository = Path(__file__).resolve().parents[2]
    manifest = json.loads((root/'manifest.json').read_text())
    if args.source_snapshot:
        require(args.source_snapshot.resolve() == (root/manifest['source_snapshot']).resolve(), 'unexpected source snapshot')
        with tarfile.open(args.source_snapshot, 'r:gz') as archive:
            for name, digest in manifest['source_sha256'].items():
                member = archive.extractfile(name)
                require(member is not None and hashlib.sha256(member.read()).hexdigest() == digest, 'changed archived source: '+name)
    else:
        for name, digest in manifest['source_sha256'].items():
            require(hashlib.sha256((repository/name).read_bytes()).hexdigest() == digest, 'changed source: '+name)
    for name, digest in manifest['evidence_sha256'].items():
        require(hashlib.sha256((root/name).read_bytes()).hexdigest() == digest, 'changed evidence: '+name)
    rows = json.loads((root/'gpu-results.json').read_text())
    available = [r for r in rows if r['kind'] != 'availability']
    expected = {(b,g,a,k) for b,g,a in CELLS for k in
                (('advanced','window','canonical','camera','instancing-native','instancing-canonical')
                 if b == 'bgfx' else ('advanced','window','canonical','camera','bump'))}
    require(len(available) == len(expected) == 39, 'incomplete final GPU matrix')
    require({(r['backend'],r['gpu'],r['api'],r['kind']) for r in available} == expected, 'duplicate/missing GPU cell')
    maximum_depth = 0.0
    for row in available:
        log = (root/row['log']).read_text()
        require(row['returncode'] == 0 and row['api_verified'] and row['physical_gpu_verified'], 'unverified GPU: '+row['log'])
        require('FAIL' not in log, 'failed test: '+row['log'])
        if row['kind'] in ('advanced','window'):
            count = 188 if row['kind'] == 'advanced' else 192
            require(f'B01 advanced PASS: {count} cases failures=0' in log, 'incomplete cases: '+row['log'])
            results = re.findall(r'advanced revision=\d+ case=\S+ coverage_xor=(\d+) max_rgba=(\d+) mae_rgb=(\S+) max_depth=(\S+) result=(\w+)',log)
            require(len(results) == (188 if row['kind'] == 'advanced' else 190), 'missing per-case results')
            if row['kind'] == 'window':
                rtt = re.findall(r'window_rtt revision=\d+ mode=(\d+) max_rgba_error=(\d+) result=(\w+)',log)
                require(len(rtt) == 2 and {int(x[0]) for x in rtt} == {0,1} and all(int(x[1]) <= 1 and x[2] == 'PASS' for x in rtt), 'window RTT incomplete')
            require(all(int(x[0]) == 0 and int(x[1]) <= 4 and float(x[2]) <= 1.5 and float(x[3]) <= 1e-5 and x[4] == 'PASS' for x in results), 'geometry/texture/depth gate failed')
            if row['kind'] == 'advanced':
                maximum_depth = max(maximum_depth, *(float(x[3]) for x in results))
            if (row['backend'],row['gpu'],row['api'],row['kind']) == ('wgpu','amd','opengl','window'):
                require('window_capture=X11-presented-RGB' in log, 'missing presented RGB observer')
                require(len(re.findall(r'window_present_stamp revision=\d+ fresh=1',log)) == count, 'stale presented frame')
            if row['backend'] == 'bgfx':
                require('instancing=1 instances=400' in log, 'BGFX instance path not exercised')
            else:
                require('rust_instances count=400' in log, 'wgpu instance path not exercised')
        elif row['kind'] == 'camera':
            require(len(re.findall(r'canonical-CPU frame=\d+ coverage_xor=0',log)) == 18, 'camera oracle incomplete')
        elif row['kind'] == 'canonical':
            require('PASS: 25 image cases' in log and log.count('coverage RTT mode=') == 2, 'B01.1/RTT incomplete')
        elif row['kind'] == 'bump':
            require('B12 PASS: 11 cases' in log, 'bump regression incomplete')
        else:
            require('instancing=1 instances=400' in log, 'B02 lost instancing')
    unavailable = [r for r in rows if r['kind'] == 'availability']
    require(len(unavailable) == 1 and unavailable[0]['returncode'] == 77, 'unavailable cell mislabeled')
    shadow_rows = []
    for name in ('shadow-results.json','shadow-extended-results.json'):
        shadow_rows += json.loads((root/name).read_text())
    cases = {('--wiring',),('--viewport',),('--quality','1'),('--quality','4'),('--quality','8'),
             ('--alpha-rtt',),('--shadow-oit','4','1'),('--shadow-oit','4','2')}
    require(len(shadow_rows) == 56 and {(r['backend'],r['gpu'],r['api'],tuple(r['case'])) for r in shadow_rows} ==
            {(b,g,a,c) for b,g,a in CELLS for c in cases}, 'incomplete shadow matrix')
    for row in shadow_rows:
        require(row['rc'] == 0 and row['api_verified'] and row['physical_gpu_verified'], 'shadow GPU not proved')
        require('shadow_coverage_policy=canonical' in (root/row['log']).read_text(), 'shadow policy absent')
    p20 = root/'p20-final'
    for gpu in ('amd','nvidia'):
        data = json.loads((p20/f'{gpu}-canonical/results.json').read_text())
        require(len(data['runs']) == 16 and not data['failures'], 'P20 canonical incomplete')
        index = {(r['target'],r['scene'],r['backend']):r for r in data['runs']}
        for target in ('window','offscreen'):
            for scene in SCENES:
                for backend in BACKENDS[1:]:
                    metrics = rgb_metrics(p20/'cpu'/f'cpu-{target}-{scene}.ppm',p20/f'{gpu}-canonical'/f'{gpu}-{target}-{scene}-{backend}.ppm')
                    require(metrics['max'] <= 2 and metrics['mae'] <= 1, 'P20 CPU pixels failed')
                require(index[target,scene,'bgfx-vulkan']['rgba_fnv64'] == index[target,scene,'wgpu-vulkan']['rgba_fnv64'], 'Vulkan RGBA changed')
    p20_summary = json.loads((p20/'portable-summary.json').read_text())
    require(not p20_summary['failures'] and len(p20_summary['canonical_cells']) == 24 and len(p20_summary['cross_gpu']) == 12 and all(r['passed'] for r in p20_summary['cross_gpu']), 'P20 cross-GPU incomplete')
    native = json.loads((root/'native-camera-results.json').read_text())
    require(len(native) == 4 and {(r['gpu'],r['api']) for r in native} == {(g,a) for g in ('amd','nvidia') for a in ('vulkan','opengl')}, 'native camera matrix incomplete')
    require(all(r['returncode'] == (1 if (r['gpu'],r['api']) == ('amd','vulkan') else 0) and r['api_verified'] and r['physical_device_verified'] for r in native), 'native control changed unexpectedly')
    require(sum(x['passed_rgb'] for x in p20_summary['native_controls']) == 22 and sum(x['total_rgb'] for x in p20_summary['native_controls']) == 24, 'native P20 failure relabeled')
    xml = ET.parse(root/'cpu-abi.xml')
    tests = xml.findall('.//testcase')
    require(len(tests) == 4 and not xml.findall('.//failure') and not xml.findall('.//skipped'), 'CPU/ABI incomplete')
    for name,count in [('rust-shader-profile.log',4),('rust-shadow-layout.log',1)]:
        require(f'test result: ok. {count} passed; 0 failed' in (root/name).read_text(), 'Rust shader tests failed')
    precision = json.loads((root/'instanced-spirv-precision.json').read_text())
    require(precision['NoContraction_count'] >= 6, 'missing SPIR-V precision')
    result = {'scope':'B01 Linux available profiles; external platforms remain unqualified',
              'supported_gpu_processes_passed':95,'offscreen_cells':7,'offscreen_cases_per_cell':188,
              'window_cells':7,'window_cases_per_cell':192,'window_native_rgba_cells':6,'window_x11_rgb_cells':1,
              'shadow_processes_passed':56,'p20_canonical_cells_passed':24,'cpu_abi_tests_passed':4,
              'rust_shader_layout_tests_passed':5,'maximum_offscreen_depth_error':maximum_depth,
              'native_camera_passes':3,'native_camera_known_failures':1,'native_p20_passes':22,
              'native_p20_comparisons':24,'unavailable_wgpu_nvidia_opengl':True,
              'source_and_evidence_hashes_verified':True,
              'source_verification_mode':'archived B01 sources' if args.source_snapshot else 'current checkout'}
    (root/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
