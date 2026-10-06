#!/usr/bin/env python3
"""Validate a relocated capture-camera-basis evidence archive, read-only.

Run the archived validate-archive.py in docs/validation/capture-camera-basis-linux,
or pass --evidence and --repository-root while testing the /tmp copy. No build,
benchmark, GPU, Git, network, figure generation or file mutation is performed.
RGB defaults to recorded metrics, hashes and preserved capture-log digests;
--recompute-rgb-if-available additionally opens all original PPMs if available.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
STAGE = 'capture-camera-basis-linux'
REPORT = 'coin-render-capture-camera-basis-linux.md'
FIGURE = 'validation/capture-camera-basis-linux/capture-camera-basis.png'
VARIANTS = ('coingl', 'bgfx-vulkan', 'bgfx-opengl', 'wgpu-vulkan')
BASELINE = {'coingl': '4d63bb993022ee8d40802558b0871a4803002b8d',
            'bgfx-vulkan': '6182410f5789bbdc30d5ccd06fa341e1810aef8e',
            'bgfx-opengl': '6182410f5789bbdc30d5ccd06fa341e1810aef8e',
            'wgpu-vulkan': '199b0e02b4f0b74ffb5d042d98e2837aafc31ad8'}
EXPECTED_COUNTS = {'cold': {'processes': 63, 'measured_frames': 63, 'warmup_frames': 0},
                   'steady': {'processes': 105, 'measured_frames': 1575, 'warmup_frames': 525},
                   'ablation': {'processes': 18, 'measured_frames': 18, 'warmup_frames': 0}}
STEADY_CASES = {'static', 'camera', 'transforms-10', 'materials-10', 'geometry-10'}
VERIFY_CASES = STEADY_CASES | {'transforms-100', 'geometry-100'}
OPTOUT = 'COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE'
CORE_TESTS = {'CoinRenderPlanAssemblyCoreTest', 'CoinRenderFrameCoreTest'}
MIXED_TESTS = {'CoinRenderActionTest', 'CoinRenderFrameReuseCoreTest'}
FINAL_GATE_TESTS = {
    'core': CORE_TESTS,
    'wgpu-action-reuse': MIXED_TESTS,
    'bgfx-action-reuse': MIXED_TESTS,
    'wgpu-vulkan-camera-transparency': {'CoinRenderCameraReuseReferenceTest', 'CoinRenderTransparencyTest'},
    'bgfx-vulkan-camera-transparency': {'CoinRenderCameraReuseReferenceTest', 'CoinRenderTransparencyTest'},
    'bgfx-opengl-camera-transparency': {'CoinRenderCameraReuseReferenceTest', 'CoinRenderTransparencyTest'},
    'wgpu-rtt-runtime': {'CoinRenderSceneTextureTest', 'CoinRenderSceneTextureDirectTest',
                         'CoinWgpuMultiDeviceTest', 'CoinRenderAsyncActionTest'},
    'bgfx-rtt': {'CoinRenderRttOwnership_vulkan', 'CoinRenderRttOwnership_opengl'}}


def require(condition, context):
    if not condition:
        raise ValueError(context)


def read(path):
    return json.loads(path.read_text(encoding='utf-8'), parse_constant=lambda value:
                      (_ for _ in ()).throw(ValueError('Nonfinite JSON: '+value)))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def valid_sha(value, length=64):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{'+str(length)+'}', value) is not None


def module(name, path):
    require(path.is_file(), 'Missing archived helper: '+str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def inside(root, relative):
    require(isinstance(relative, str) and not Path(relative).is_absolute(), 'Archive path must be relative: '+str(relative))
    path = (root/relative).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(), 'Missing/outside archive file: '+str(path))
    return path


def inventory_check(evidence, repository):
    inventory = read(evidence/'stage-files.json')
    require(inventory.get('inventory_excludes_itself') is True, 'Inventory must explicitly exclude itself')
    records = inventory.get('files', [])
    expected = {item['path'] for item in records}
    require(len(expected) == len(records), 'Duplicate stage inventory paths')
    inventory_path = str((evidence/'stage-files.json').relative_to(repository))
    require(inventory_path not in expected, 'Inventory includes itself')
    actual = {str(path.relative_to(repository)) for path in evidence.rglob('*')
              if path.is_file() and path != evidence/'stage-files.json'}
    actual.add('docs/'+REPORT)
    require(actual == expected, 'Stage inventory file set differs: missing='+str(sorted(expected-actual))+' extra='+str(sorted(actual-expected)))
    for item in records:
        path = inside(repository, item['path'])
        require(valid_sha(item.get('sha256')) and type(item.get('bytes')) is int and item['bytes'] >= 0,
                'Invalid inventory hash/bytes: '+item['path'])
        require(path.stat().st_size == item['bytes'] and sha(path) == item['sha256'], 'Stage inventory mismatch: '+str(path))
    for key in ('file_count', 'files_count', 'count'):
        if key in inventory:
            require(inventory[key] == len(records), 'Inventory declared count differs: '+key)
    return len(records)


def summary_path(directory):
    paths = [directory/name for name in ('summary.json', 'report-summary.json') if (directory/name).is_file()]
    require(len(paths) == 1, 'Require exactly one summary JSON in '+str(directory))
    return paths[0]


def copied_files_check(directory):
    archive = read(directory/'archive-manifest.json')
    records = archive['copied_files']
    for item in records:
        path = inside(directory, item['archive_path'])
        require(path.stat().st_size == item['bytes'] and sha(path) == item['sha256'], 'Original copy mismatch: '+str(path))
    require(archive.get('runner_json_preserved_verbatim') is True, 'Runner JSON preservation is not recorded')
    require(archive.get('ppm_files_copied') is False, 'Archive must record that original PPMs were not copied')
    return len(records)


def sources_check(manifest, side, metadata):
    expected = BASELINE if side == 'before' else {variant: metadata['coingl_source_content_revision'] if variant == 'coingl'
                                                 else metadata['source_content_revision'] for variant in VARIANTS}
    sources = manifest.get('variant_source_content_revisions', manifest.get('variant_sources'))
    require(sources == expected, 'Per-variant source map differs: '+side)
    if side == 'before':
        require(manifest.get('source_content_revision') is None, 'Mixed baseline must not claim one content revision')
    else:
        require(manifest.get('source_content_revision') == metadata['source_content_revision'], 'After content revision differs')
    require(manifest.get('scene_sha256') == metadata['scene_sha256'], 'Scene SHA differs')
    for record in manifest.get('commands', []):
        variant = record.get('variant')
        if not variant:
            variant = next((candidate for candidate in VARIANTS if '-'+candidate+'-' in record.get('stem', '')), None)
        require(variant in expected and record.get('source_content_revision') == expected[variant],
                'Command source differs: '+record.get('stem', '?'))
        if 'exit_code' in record:
            require(record['exit_code'] == 0, 'Archived campaign command did not succeed')
        require(not record.get('timed_out'), 'Archived campaign command timed out')
        require(OPTOUT not in record.get('environment', {}), 'Timing/verification command inherited capture optout')


def normalize(value, replacements):
    if isinstance(value, str):
        for source, target in replacements:
            value = value.replace(source, target)
        return value
    if isinstance(value, list):
        return [normalize(item, replacements) for item in value]
    if isinstance(value, dict):
        return {normalize(key, replacements): normalize(item, replacements) for key, item in value.items()}
    return value


def analysis_check(evidence, metadata):
    trace = module('capture_trace_archive', evidence/'diagnostic/derive-diagnostic.py')
    analysis = module('capture_analysis_archive', evidence/'analyze-capture.py')
    stored = read(evidence/'analysis.json')
    require(set(stored['campaigns']) == {'cold', 'steady'}, 'Analysis campaign coverage differs')
    require(len(stored['diagnostics']) == 1, 'Require exactly one diagnostic archive')
    replacements = []
    for name in ('cold', 'steady'):
        for side in ('before', 'after'):
            token = '<'+name+'-'+side+'>'
            replacements.extend([(stored['campaigns'][name][side]['directory'], token),
                                 (str((evidence/name/side).resolve()), token)])
    diagnostic = next(iter(stored['diagnostics'].values()))
    replacements.extend([(diagnostic['metadata_and_raw_trace']['input_directory'], '<diagnostic>'),
                         (str((evidence/'diagnostic').resolve()), '<diagnostic>')])
    replacements = sorted(set(replacements), key=lambda item: len(item[0]), reverse=True)
    all_counts = Counter()
    for name in ('cold', 'steady'):
        recomputed = analysis.compare_campaigns(trace, name, evidence/name/'before', evidence/name/'after')
        require(normalize(recomputed, replacements) == normalize(stored['campaigns'][name], replacements), 'Matched analysis recomputation differs: '+name)
        require(recomputed['complete_and_comparable'] and recomputed['unique_counts'] == EXPECTED_COUNTS[name], 'Matched counts/protocol incomplete: '+name)
        expected_cases = {'static'} if name == 'cold' else STEADY_CASES
        require(set(recomputed['comparisons']) == {case+'|'+variant for case in expected_cases for variant in VARIANTS}, 'Matched group coverage differs: '+name)
        require(all(control['counted_once'] for control in recomputed['shared_controls']), 'Shared CoinGL CSV/log identity is not proven')
        for side in ('before', 'after'):
            sources_check(recomputed[side]['metadata'], side, metadata)
            parameters = recomputed[side]['metadata']['parameters']
            require(int(parameters['rounds']) == (9 if name == 'cold' else 3) and
                    int(parameters['frames']) == (1 if name == 'cold' else 15) and
                    int(parameters['warmup']) == (0 if name == 'cold' else 5), 'Matched sample protocol differs')
        all_counts.update(recomputed['unique_counts'])
    require(dict(all_counts) == {'processes': 168, 'measured_frames': 1638, 'warmup_frames': 525}, 'Combined matched counts differ')
    require(stored.get('unique_counts') == dict(all_counts), 'Stored aggregate matched counts differ')
    require(stored.get('campaign_counts_match_expected') is True, 'Orchestration expected counts were not matched')
    recomputed = analysis.read_diagnostic(trace, evidence/'diagnostic')
    require(normalize(recomputed, replacements) == normalize(diagnostic, replacements), 'Diagnostic analysis recomputation differs')
    require(recomputed['complete_and_comparable'] and recomputed['unique_counts'] == EXPECTED_COUNTS['ablation'], 'Diagnostic pairing/counts incomplete')
    require(set(recomputed['group_summaries']) == {variant+'|static' for variant in VARIANTS if variant != 'coingl'}, 'Diagnostic API coverage differs')
    for run in recomputed['metadata_and_raw_trace']['runs'].values():
        require(run['capture_basis_counter_check']['passed'], 'Per-process capture basis proof differs')
        require(len(run['capture_basis_events']) == run['samples']['row_count'] == 1, 'Cold trace/CSV cardinality differs')
        require(run['samples']['measured_row_indices'] == [0] and run['samples']['warmup_row_indices'] == [], 'Diagnostic warmup selection differs')
        require(run['counter_measured_medians']['capture_camera_basis.calls'] == 1, 'Cold capture calls differ')
    require(recomputed['metadata_and_raw_trace']['command_metadata']['source_content_revision'] == metadata['source_content_revision'], 'Diagnostic source differs')
    return stored


def gate_check(directory, final, metadata):
    manifest = read(directory/'commands.json')
    commands = manifest['commands']
    expected = FINAL_GATE_TESTS if final else {name: FINAL_GATE_TESTS[name] for name in ('core', 'wgpu-action-reuse')}
    require({record['name'] for record in commands} == set(expected) and len(commands) == len(expected), 'Gate command coverage differs')
    require(valid_sha(manifest['source_content_revision'], 40), 'Gate source revision is invalid')
    if final:
        require(manifest['source_content_revision'] == metadata['source_content_revision'], 'Final gates source differs')
    elif metadata.get('initial_source_content_revision'):
        require(manifest['source_content_revision'] == metadata['initial_source_content_revision'], 'Initial gates source differs')
    statuses, categories = Counter(), Counter()
    pattern = re.compile(r'^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.{2,}\s*(.*?)\s+[\d.]+\s+sec\s*$', re.M)
    for record in commands:
        name = record['name']
        definitions = record['ctest_definitions']
        require({test['name'] for test in definitions} == expected[name] and len(definitions) == len(expected[name]), 'CTest definitions differ: '+name)
        require(record.get('skip_observed') is False, 'Gate skip is missing or observed: '+name)
        log = (directory/(name+'.log')).read_text()
        results = pattern.findall(log)
        require(len(results) == len(definitions) and {test for test, _ in results} == expected[name], 'CTest result coverage differs: '+name)
        require(all(status in ('Passed', '***Failed') for _, status in results), 'Unknown/skipped CTest status: '+name)
        passed = sum(status == 'Passed' for _, status in results)
        failed = len(results)-passed
        require((record['exit_code'] == 0) == (failed == 0), 'CTest exit/status disagree: '+name)
        summary = re.search(r'(\d+)% tests passed, (\d+) tests failed out of (\d+)', log)
        require(summary and int(summary[2]) == failed and int(summary[3]) == len(results), 'CTest summary differs: '+name)
        last = (directory/(name+'-ctest.log')).read_text()
        blocks = re.split(r'^\d+/\d+ Testing:\s*', last, flags=re.M)[1:]
        last_statuses = {}
        for block in blocks:
            test = block.splitlines()[0].strip()
            outcomes = re.findall(r'^Test (Passed|Failed)\.$', block, re.M)
            require(len(outcomes) == 1 and test not in last_statuses, 'CTest full-log status incomplete: '+name)
            last_statuses[test] = outcomes[0]
        require(last_statuses == {test: 'Passed' if status == 'Passed' else 'Failed' for test, status in results}, 'CTest summary/full-log disagree: '+name)
        statuses.update({'passed': passed, 'failed': failed, 'executions': len(results)})
        category = 'Core puro' if name == 'core' else 'Action/Reuse misto' if name.endswith('action-reuse') else 'GPU'
        categories[category] += len(results)
    if final:
        require(dict(statuses) == {'passed': 18, 'failed': 0, 'executions': 18}, 'Final gate count/results differ')
        require(dict(categories) == metadata['gate_counts'] == {'Core puro': 2, 'Action/Reuse misto': 4, 'GPU': 12}, 'Final gate categories differ')
    else:
        require(dict(statuses) == {'passed': 3, 'failed': 1, 'executions': 4}, 'Initial gate attempt was not preserved')
    return dict(statuses)


def cli_initial_check(evidence, metadata):
    directory = evidence/'diagnostic-cli-initial'
    manifest = read(directory/'commands.json')
    require(manifest['source_content_revision'] == metadata['source_content_revision'] and
            manifest['scene_sha256'] == metadata['scene_sha256'], 'Initial rejected CLI source/scene differs')
    require(len(manifest['commands']) == 1, 'Initial rejected CLI attempt count differs')
    record = manifest['commands'][0]
    command = record['command']
    require(command[command.index('--animated-percent')+1] == '0', 'Initial CLI percentage rejection was not retained')
    log = (directory/(record['stem']+'.log')).read_text()
    require(log.strip() == 'Invalid animation percentage or step' and not list(directory.rglob('*.csv')),
            'Initial rejected CLI must contain no rendered/measured frame')
    if 'exit_code' in record:
        require(record['exit_code'] != 0, 'Rejected CLI unexpectedly succeeded')
    return manifest


def capture_records(directory, campaign):
    records = {}
    for process in campaign['processes']:
        log = inside(directory, process['log']).read_text()
        for line in log.splitlines():
            if not line.startswith('capture '):
                continue
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            identity = (process['case'], process['variant'], process['round'], int(fields['logical_frame']))
            require(identity not in records, 'Duplicate captured logical frame')
            records[identity] = fields
    return records


def rgb_check(evidence, summary, datasets, open_pixels=False):
    rgb = read(evidence/'steady/rgb-before-after.json')
    require(rgb == summary['rgb_before_after'], 'Summary and standalone RGB record differ')
    results = rgb['results']
    require(rgb['comparison_count'] == len(results) == 196 and rgb['all_rgb_identical'] is True, 'Recorded RGB count/result differs')
    expected = {(case, variant, 1, frame*100) for case in VERIFY_CASES for variant in VARIANTS for frame in range(7)}
    identities = {(item['case'], item['variant'], item['round'], item['logical_frame']) for item in results}
    require(len(identities) == len(results) and identities == expected, 'RGB case/variant/logical-frame coverage differs')
    captures = {side: capture_records(evidence/'steady'/('verify-'+side), datasets['verify-'+side]) for side in ('before', 'after')}
    require(all(set(values) == expected for values in captures.values()), 'RGB capture-log coverage differs')
    originals = []
    for item in results:
        identity = (item['case'], item['variant'], item['round'], item['logical_frame'])
        require(all(item[key] == 0 for key in ('rgb_mae', 'max_channel_error', 'pixels_different', 'pixels_over3')), 'Recorded RGB errors differ')
        require(valid_sha(item['before']['ppm_sha256']) and item['before']['ppm_sha256'] == item['after']['ppm_sha256'], 'Recorded PPM SHA differs')
        pair = []
        for side in ('before', 'after'):
            captured, stored = captures[side][identity], item[side]
            require(captured['state_fnv64'] == item['state_fnv64'] and
                    captured['rgb_fnv64'] == stored['rgb_fnv64'] and captured['rgba_fnv64'] == stored['rgba_fnv64'], 'RGB digest/capture-log mismatch')
            require(captured['image'] == stored['original_image'], 'Original PPM reference differs')
            pair.append(Path(stored['original_image']))
        originals.append((item, pair))
    recomputed = False
    if open_pixels and all(path.is_file() for _, pair in originals for path in pair):
        from PIL import Image
        for item, pair in originals:
            pixels = []
            for side, path in zip(('before', 'after'), pair):
                require(sha(path) == item[side]['ppm_sha256'], 'Available original PPM hash differs')
                with Image.open(path) as image:
                    require(image.mode == 'RGB', 'Original PPM must be RGB')
                    pixels.append((image.size, image.tobytes()))
            require(pixels[0] == pixels[1], 'Available original RGB pixels differ')
        recomputed = True
    return {'pairs_recorded': len(results), 'pixels_recomputed': recomputed,
            'original_ppms_available': all(path.is_file() for _, pair in originals for path in pair)}


def binary_check(evidence, metadata, manifests):
    post = read(evidence/'binary-hashes-post-campaign.json')
    records = post['files']
    require(post['all_unchanged'] is True and len(records) == 20 and all(item['unchanged'] is True for item in records), 'Post-campaign binary verification count differs')
    require(len({item['path'] for item in records}) == 20, 'Duplicate post-campaign binary paths')
    categories, verified = Counter(), {}
    snapshots = {stage: read(evidence/name) for stage, name in
                 (('baseline', 'binaries-before.json'), ('final', 'binaries-after.json'),
                  ('coingl', 'binaries-coingl.json'))}
    require({stage: len(snapshot['files']) for stage, snapshot in snapshots.items()} ==
            {'baseline': 8, 'final': 8, 'coingl': 4}, 'Original binary snapshot counts differ')
    for item in records:
        require(valid_sha(item['sha256']) and item.get('post_sha256') == item['sha256'],
                'Invalid/changed post-campaign binary SHA')
        require(item.get('stage') in snapshots, 'Unknown post-campaign binary stage')
        snapshot = snapshots[item['stage']]
        original = [entry for entry in snapshot['files'] if entry['path'] == item['path']]
        require(len(original) == 1 and original[0]['sha256'] == item['sha256'] and
                original[0]['role'] == item['role'], 'Post-campaign SHA/role differs from original binary snapshot')
        if 'bytes' in original[0]:
            require(original[0]['bytes'] == item.get('bytes'), 'Post-campaign binary byte count differs')
        path = item['path']
        category = 'before' if '/coin-render-capture-baseline/' in path else 'control' if '/coingl/' in path else 'after'
        require({'before': 'baseline', 'after': 'final', 'control': 'coingl'}[category] == item['stage'],
                'Post-campaign stage/path classification differs')
        categories[category] += 1
        source = original[0].get('source_content_revision', snapshot.get('source_content_revision'))
        expected = BASELINE['coingl'] if category == 'control' else metadata['source_content_revision'] if category == 'after' else BASELINE['wgpu-vulkan'] if original[0]['role'] == 'wgpu' else BASELINE['bgfx-vulkan']
        require(source == expected, 'Binary snapshot source differs: '+path)
        if 'source_content_revision' in item:
            require(item['source_content_revision'] == expected, 'Post-campaign source differs: '+path)
        verified[path] = item['sha256']
    require(dict(categories) == {'before': 8, 'after': 8, 'control': 4}, 'Post-campaign binary role counts differ')
    for manifest in manifests:
        for path, digest in manifest['binary_hashes'].items():
            alternatives = (path, path+'.0.10') if path.endswith('/libCoin.so.80') else (path,)
            require(any(verified.get(candidate) == digest for candidate in alternatives), 'Manifest binary SHA is absent/different in post-campaign snapshots: '+path)
    return len(records)


def validate(evidence, repository, pixels=False):
    inventory_count = inventory_check(evidence, repository)
    metadata = read(evidence/'stage-metadata.json')
    require(valid_sha(metadata.get('source_content_revision'), 40), 'Invalid current content revision')
    require(metadata.get('baseline_variant_source_content_revisions') == BASELINE and
            metadata.get('baseline_source_content_revision') is None, 'Mixed baseline source metadata differs')
    require(metadata.get('coingl_source_content_revision') == BASELINE['coingl'] and valid_sha(metadata.get('scene_sha256')), 'Control/scene source metadata differs')
    require(metadata.get('expected_counts') == EXPECTED_COUNTS, 'Expected stage counts differ')
    summaries, manifests, copied, comparison_count, verification = {}, [], 0, 0, {}
    for name in ('cold', 'steady'):
        directory = evidence/name
        copied += copied_files_check(directory)
        core = module('capture_core_'+name, directory/'campaign_core.py')
        summarizer = module('capture_summary_'+name, directory/'summarize.py')
        stored = read(summary_path(directory));summaries[name] = stored
        before = core.summarize_campaign('before', directory/'before')
        after = core.summarize_campaign('after', directory/'after')
        timing = summarizer.compare_timing(core, before, after)
        require(timing == stored['timing'], 'Timing comparison recomputation differs: '+name)
        comparison_count += len(timing['comparisons'])
        for side, campaign in (('before', before), ('after', after)):
            require(campaign['groups'] == stored['campaigns'][side]['groups'], 'Group recomputation differs: '+name+'/'+side)
            sources_check(campaign['manifest'], side, metadata);manifests.append(campaign['manifest'])
        if name == 'steady':
            for label in ('verify-before', 'verify-after'):
                campaign = core.summarize_campaign(label, directory/label)
                require(campaign['groups'] == stored['campaigns'][label]['groups'], 'Verification CSV recomputation differs: '+label)
                require(len(campaign['processes']) == 28 and len(campaign['groups']) == 28, 'Verification process count differs')
                sources_check(campaign['manifest'], label.removeprefix('verify-'), metadata)
                manifests.append(campaign['manifest']);verification[label] = campaign
    require(comparison_count == 24, 'Timing comparison count differs')
    analysis = analysis_check(evidence, metadata)
    final_gates = gate_check(evidence/'gates', True, metadata)
    initial_gates = gate_check(evidence/'gates-initial', False, metadata)
    initial_cli = cli_initial_check(evidence, metadata)
    manifests.append(initial_cli)
    manifests.append(next(iter(analysis['diagnostics'].values()))['metadata_and_raw_trace']['command_metadata'])
    rgb = rgb_check(evidence, summaries['steady'], verification, pixels)
    require(metadata.get('rgb_comparisons') == rgb['pairs_recorded'] == 196 and
            metadata.get('verification_processes') == 56 and metadata.get('verification_ppm_files') == 392 and
            metadata.get('all_rgb_identical') is True, 'RGB metadata counts/results differ')
    reporter = module('capture_report_archive', evidence/'plot-and-report.py')
    groups, counts, report_rgb = reporter.validate(metadata, analysis, summaries['cold'], summaries['steady'])
    markdown = reporter.report(metadata, analysis, groups, counts, report_rgb, FIGURE)
    require(markdown == (repository/'docs'/REPORT).read_text(), 'Report regeneration differs')
    inputs = read(evidence/'report-inputs.json')
    for name, path in (('cold', summary_path(evidence/'cold')), ('steady', summary_path(evidence/'steady')),
                       ('analysis', evidence/'analysis.json'), ('metadata', evidence/'stage-metadata.json')):
        require(sha(path) == inputs['inputs'][name]['sha256'], 'Report input hash differs: '+name)
    require(sha(evidence/'plot-and-report.py') == inputs['generator']['sha256'], 'Report generator hash differs')
    require(set(inputs['inputs']) == {'cold', 'steady', 'analysis', 'metadata'} and
            set(inputs['outputs']) == {REPORT, 'capture-camera-basis.png', 'capture-camera-basis.svg'},
            'Report input/output coverage differs')
    for name, digest in inputs['outputs'].items():
        path = repository/'docs'/name if name.endswith('.md') else inside(evidence, name)
        require(sha(path) == digest, 'Report output hash differs: '+name)
    binary_count = binary_check(evidence, metadata, manifests)
    return {'stage_files_verified': inventory_count, 'copied_files_verified': copied,
            'timing_comparisons_recomputed': comparison_count, 'matched_processes': 168,
            'matched_measured_frames': 1638, 'matched_warmup_frames': 525,
            'diagnostic_processes_recomputed': 18, 'rgb_pairs_recorded': rgb['pairs_recorded'],
            'rgb_pixels_recomputed': rgb['pixels_recomputed'], 'original_ppms_available': rgb['original_ppms_available'],
            'final_gates': final_gates, 'initial_attempt_retained': initial_gates,
            'rejected_cli_attempts_excluded': 1,
            'post_campaign_binary_hashes_verified': binary_count, 'report_regeneration_identical': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--evidence', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--repository-root', type=Path)
    parser.add_argument('--recompute-rgb-if-available', action='store_true')
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    repository = args.repository_root.resolve() if args.repository_root else evidence.parents[2]
    require(evidence.is_relative_to(repository) and evidence.is_dir(), 'Evidence/repository layout is invalid')
    print(json.dumps(validate(evidence, repository, args.recompute_rgb_if_available), indent=2))


if __name__ == '__main__':
    main()
