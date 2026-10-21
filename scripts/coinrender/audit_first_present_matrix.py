#!/usr/bin/env python3
"""Verify B03 observer evidence without upgrading the historical issue to fixed."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile


def require(value, message):
    if not value:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    root = args.evidence
    manifest = json.loads((root/'manifest.json').read_text())
    for name, digest in manifest['evidence_sha256'].items():
        require(hashlib.sha256((root/name).read_bytes()).hexdigest() == digest, 'changed evidence '+name)
    with tarfile.open(root/manifest['source_snapshot'], 'r:gz') as archive:
        for name, digest in manifest['source_sha256'].items():
            member = archive.extractfile(name)
            require(member is not None and hashlib.sha256(member.read()).hexdigest() == digest, 'changed archived source '+name)
    rows = json.loads((root/'results.json').read_text())
    cells = {(b,g,a) for b in ['bgfx','wgpu'] for g in ['amd','nvidia'] for a in ['vulkan','opengl']
             if (b,g,a) != ('wgpu','nvidia','opengl')}
    terrain = manifest.get('terrain_matrix', False)
    variants = ['terrain-native-managed','terrain-canonical-managed','terrain-double-managed'] if terrain else ['million-native','million-canonical','million-managed','small-native']
    expected = {(b,g,a,v) for b,g,a in cells for v in variants}
    if not terrain:
        expected |= {(b,g,a,'overdraw-native') for b,g,a in cells if b == 'wgpu'}
    expected |= {('wgpu','amd','opengl','repeat-managed-%02d' % n) for n in range(manifest['gl_repetitions'])}
    expected |= {('wgpu','amd','opengl','negative-stale'),('wgpu','nvidia','opengl','availability')}
    require(len(rows) == len(expected) and {(x['backend'],x['gpu'],x['api'],x['variant']) for x in rows} == expected,
            'duplicate or missing physical/API/fixture cell')
    positive=0; immediate_stale=0; fixed_stale=0; gl_fixed_stale=0
    for row in rows:
        log = (root/row['log']).read_text()
        if row['variant'] == 'availability':
            require(row['returncode'] == 77 and not row['observations'], 'unavailable cell counted as rendering')
            continue
        require(row['api_verified'] and row['physical_gpu_verified'], 'unverified physical API '+row['log'])
        require('first_present_adapter ' in log, 'missing adapter receipt')
        observations = re.findall(r'first_present frame=(\d+) triangles=(\d+) extent=(\d+)x(\d+) immediate_fresh=(\d+) fixed_50ms_fresh=(\d+) eventual_fresh=(\d+) elapsed_ms=(\S+) polls=(\d+) serial=(\d+) serial_unchanged=(\d+) no_new_render=1', log)
        if row['variant'] == 'negative-stale':
            require(row['returncode'] == 1 and len(observations) == 2 and
                    observations[-1][6] == '0' and observations[-1][10] == '0' and
                    'B03 single-submit observer FAIL' in log, 'stale-image negative control accepted')
            continue
        positive += 1
        require(row['returncode'] == 0 and 'B03 single-submit observer PASS' in log, 'failed observer '+row['log'])
        require(len(observations) == 3 and [int(x[0]) for x in observations] == [0,1,2] and
                all(x[6] == '1' and x[10] == '1' for x in observations), 'missing fresh single submissions')
        if terrain:
            if manifest.get('resize_prepare_fix') and row['backend'] == 'bgfx' and row['api'] == 'opengl':
                require(log.count('bgfx_window_resize_prepare size=640x480 scene_draws=0') == 1, 'GL drawable preparation missing or duplicated')
            pixels = re.findall(r'first_present_terrain_pixels frame=(\d+) mismatches=0 max_rgb=0 foreground=(\d+) reference_after_capture=1 result=PASS', log)
            require([x[0] for x in pixels] == ['0','1','2'] and all(int(x[1]) > int(o[2])*int(o[3])//10 for x,o in zip(pixels,observations)), 'terrain full-image/coverage oracle failed')
            require('vertices=3000000 indices=3000000 draws=20' in log, 'historical terrain capture not exercised')
            submissions = 2 if row['variant'] == 'terrain-double-managed' else 1
            require(f'first_present_workload terrain=1 public_action=1 submissions_per_observation={submissions}' in log, 'wrong capture or submission path')
        else:
            require(re.findall(r'first_present_pixels frame=(\d+) mismatches=0 max_rgb=0',log) == ['0','1','2'], 'independent full-image rectangle oracle failed')
        require([(int(x[2]),int(x[3])) for x in observations] == [(1280,960),(1280,960),(640,480)], 'resize missing')
        triangles = 2 if row['variant'] == 'small-native' else 16000000 if row['variant'] == 'overdraw-native' else 1000000
        require(all(int(x[1]) == triangles for x in observations), 'wrong geometry workload')
        immediate_stale += sum(x[4] == '0' for x in observations)
        fixed_stale += sum(x[5] == '0' for x in observations)
        if row['backend'] == 'wgpu' and row['api'] == 'opengl':
            gl_fixed_stale += sum(x[5] == '0' for x in observations)
    regressions_verified = False
    if terrain and manifest.get('window_regressions'):
        regression_root = root/manifest['window_regressions']
        regressions = json.loads((regression_root/'results.json').read_text())
        kinds = ['coverage-window','multi-object','multi-weighted','multi-peeling','multi-offscreen-first']
        require(len(regressions) == 20 and {(x['gpu'],x['api'],x['kind']) for x in regressions} ==
                {(g,a,k) for g in ['amd','nvidia'] for a in ['vulkan','opengl'] for k in kinds}, 'window regression matrix incomplete')
        for row in regressions:
            log = (regression_root/row['log']).read_text()
            require(row['returncode'] == 0 and row['api_verified'] and row['physical_gpu_verified'] and 'FAIL' not in log, 'window regression failed')
            if row['kind'] == 'coverage-window':
                require('B01 advanced PASS: 192 cases failures=0' in log, 'B01 window regression incomplete')
            else:
                require('multiwindow_x11_pixels=required' in log and 'BGFX simultaneous native targets and offscreen lifecycle passed' in log and
                        len(re.findall(r'window=\d+ center=',log)) >= 12, 'multiwindow pixel predicates not exercised')
        regressions_verified = True
    original_verified = False
    if terrain and manifest.get('historical_original'):
        history = root/manifest['historical_original']
        receipt = json.loads((history/'original-run.json').read_text())
        require(receipt['returncode'] == 0, 'historical fixture failed')
        require(hashlib.sha256((history/'original-main.cpp').read_bytes()).hexdigest() == receipt['source_sha256'], 'historical fixture source changed')
        for stage,expected_coverage in [('initial',401466),('final',400710)]:
            images = []
            for kind in ['window','offscreen']:
                magic,size,maximum,data = (history/f'original-01-{stage}-{kind}.ppm').read_bytes().split(b'\n',3)
                require(magic == b'P6' and size == b'1280 960' and maximum == b'255' and len(data) == 1280*960*3, 'invalid original PPM')
                images.append(data)
            require(images[0] == images[1], 'original window/offscreen pixels differ')
            coverage = sum(bool(images[0][i] or images[0][i+1] or images[0][i+2]) for i in range(0,len(images[0]),3))
            require(coverage == expected_coverage, 'historical terrain coverage changed')
        before = json.loads((root/'before/results.json').read_text())
        failures = [x for x in before if not x['passed']]
        require({(x['backend'],x['gpu'],x['api'],x['variant']) for x in failures} ==
                {('bgfx','amd','opengl',v) for v in ['terrain-native-managed','terrain-canonical-managed']}, 'unexpected pre-fix failure set')
        for row in failures:
            log = (root/'before'/row['log']).read_text()
            require(row['returncode'] == 1 and row['api_verified'] and row['physical_gpu_verified'] and
                    re.search(r'first_present frame=2 .*eventual_fresh=0 .*serial_unchanged=1 no_new_render=1',log), 'pre-fix resize failure unproved')
        original_verified = True
    result = {'terrain_matrix':terrain,'positive_processes_passed':positive,
              'fresh_frames_with_exact_offscreen_pixels' if terrain else 'fresh_frames_with_exact_independent_pixels':positive*3,
              'physical_api_cells':7,'stale_image_negative_controls_rejected':1,'unavailable_cells':1,
              'immediate_stale_observations':immediate_stale,'fixed_50ms_stale_observations':fixed_stale,
              'wgpu_opengl_fixed_50ms_stale_observations':gl_fixed_stale,
              'B03_historical_50ms_failure_cause_proved':False,'B03_closed':False,
              'B03_resize_crossing_fixed_and_verified':terrain and bool(manifest.get('resize_prepare_fix')) and original_verified and regressions_verified,
              'window_regressions_verified':regressions_verified,
              'historical_original_pixels_recomputed':original_verified,
              'evidence_hashes_verified':True}
    (root/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
