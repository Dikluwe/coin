#!/usr/bin/env python3
"""Read-only, relocatable material-table evidence validation (no GPU/build/Git)."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
BASELINE = '96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33'
CURRENT = 'a0bda8b5ccc8d04efa96c5c807b88e5aa4e34c9d'
CONTROL = '4d63bb993022ee8d40802558b0871a4803002b8d'
STAGE = 'material-table-linux'
REPORT = 'coin-render-material-table-linux.md'
OPTOUT = 'COIN_WGPU_DISABLE_MATERIAL_RESOURCE_REUSE'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text(), parse_constant=lambda value:
                      (_ for _ in ()).throw(ValueError('Nonfinite JSON: ' + value)))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def inside(root, relative):
    require(isinstance(relative, str) and not Path(relative).is_absolute(), 'Require relative archive path')
    path = (root / relative).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(), 'Missing/unsafe file: ' + str(path))
    return path


def valid_sha(value, length=64):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{' + str(length) + '}', value) is not None


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


def quiet_check(directory, helper_path):
    helper = module('material_quiet_rederive', helper_path)
    manifest = read(directory / 'commands.json')
    stored = read(directory / 'results.json')
    require(manifest.get('status') == 'completed' and manifest.get('runner', {}).get('sha256') == sha(helper_path),
            'Quiet completion/helper provenance differs')
    expected_sources = {role: CONTROL if role == 'coingl-control' else CURRENT if role == 'wgpu-after' else BASELINE
                        for role in helper.ROLES}
    require(manifest['role_source_content_revisions'] == expected_sources and
            manifest['expected_unique_counts'] == helper.EXPECTED == {'processes': 90, 'measured_frames': 2925, 'warmup_frames': 825},
            'Quiet sources/counts differ')
    require(manifest['parameters']['trace'] is False and manifest['parameters']['size'] == 1024, 'Quiet trace/size differs')
    for record in manifest['commands']:
        require(record['source_content_revision'] == expected_sources[record['role']] and
                record['scene_sha256'] == manifest['scenes'][record['scene_key']]['sha256'] and
                record['trace_environment_absent'] is True and record['disabled_flags_absent'] is True and
                record['exit_code'] == 0 and record['status'] == 'passed' and record['timed_out'] is False,
                'Quiet command source/scene/optout/status differs')
        require(all(not key.startswith(('COIN_RENDER_DISABLE_', 'COIN_WGPU_DISABLE_', 'COIN_BGFX_DISABLE_'))
                    and key not in helper.TRACE_KEYS for key in record['environment']), 'Quiet inherited a private optout/trace')
        for kind, suffix in (('stdout', '.stdout'), ('stderr', '.stderr'), ('log', '.log')):
            path = directory / ('logs' if kind == 'log' else kind) / (record['stem'] + suffix)
            card = record['raw_outputs'][kind]
            require(path.stat().st_size == card['bytes'] and sha(path) == card['sha256'], 'Quiet raw output differs')
        require((directory / 'logs' / (record['stem'] + '.log')).read_bytes() ==
                (directory / 'stdout' / (record['stem'] + '.stdout')).read_bytes() +
                (directory / 'stderr' / (record['stem'] + '.stderr')).read_bytes(), 'Quiet stdout/stderr preservation differs')
    actual = helper.derive(directory)
    require(actual == stored and actual['complete_and_comparable'] is True, 'Quiet raw recomputation differs/incomplete')
    return actual


def diagnostic_check(directory, results_path, helper_path):
    helper = module('material_diagnostic_rederive', helper_path)
    stored = read(results_path)
    actual = helper.analyze(directory)
    require(actual['command_metadata']['script_sha256'] == sha(helper_path), 'Diagnostic analyzer/executed script SHA differs')
    # analyze() includes own absolute input records; original argv/environment
    # stay verbatim. Only its resolved input-directory provenance relocates.
    original = stored['input_directory']
    replacements = [(str(directory.resolve()), '<diagnostic>'), (original, '<diagnostic>')]
    require(normalize(actual, replacements) == normalize(stored, replacements), 'Diagnostic raw recomputation differs')
    require(actual['complete_and_comparable'] is True and not actual['issues'] and
            actual['command_metadata']['source_content_revision'] == CURRENT, 'Diagnostic incomplete/source differs')
    cards = {}
    for name, expected in helper.EXPECTED.items():
        campaign = actual['campaigns'].get(name)
        require(campaign and campaign['unique_counts'] == expected and campaign['complete_and_comparable'] is True,
                'Diagnostic campaign missing/incomplete: ' + name)
        runs = [run for run in actual['runs'].values() if run['command_metadata']['campaign'] == name]
        require(len(runs) == expected['processes'] and all(run['complete_and_comparable'] and run['alignment']['passed'] for run in runs),
                'Diagnostic per-process proof incomplete')
        cards[name] = {'counts': expected, 'raw_recomputed': True, 'complete_and_comparable': True,
                       'proofs_passed': len(runs), 'results_sha256': sha(results_path)}
    return actual, cards


def rgb_check(directories, results_path, wrapper_paths, wrapper_log_paths, require_pixels=False):
    """Recheck source commands and capture records. Pixels only in precollection.

    A relocated archive preserves pixel checks/hashes and raw captures; it has no
    PPM files and must never claim it reran an image comparison.
    """
    rgb = read(results_path)
    expected_cases = {'original': {'materials-10', 'transforms-10', 'geometry-10'},
                      'large': {'materials-10', 'materials-100', 'transforms-10', 'static'}}
    captures = {}
    wrappers = {side: {record['label']: record for record in read(path)} for side, path in wrapper_paths.items()}
    require(set(directories) == {(side, scene) for side in ('before', 'after') for scene in expected_cases}, 'RGB dataset coverage differs')
    for (side, scene), directory in directories.items():
        manifest = read(directory / 'manifest.json')
        results = read(directory / 'results.json')
        require(len(results) == len(manifest['commands']) == len(expected_cases[scene]) and
                {result['case'] for result in results} == expected_cases[scene], 'RGB process coverage differs')
        source = BASELINE if side == 'before' else CURRENT
        wrapper = wrappers[side][scene]
        wrapper_log = Path(wrapper_log_paths[side, scene])
        require(wrapper['exit_code'] == 0 and wrapper['source_content_revision'] == source and
                wrapper['scene_sha256'] == manifest['scene_sha256'] and sha(wrapper_log) == wrapper['log_sha256'],
                'RGB wrapper source/exit/scene/log differs')
        wrapper_text = wrapper_log.read_text()
        require(f"Completed {len(results)} new processes; raw CSV and logs preserved" in wrapper_text,
                'RGB wrapper did not finish all processes')
        for record in results:
            require(record['variant'] == 'wgpu-vulkan' and record['round'] == 1, 'RGB process variant/round differs')
            matching = [command for command in manifest['commands'] if command['stem'] == record['case'] + '-wgpu-vulkan-1']
            require(len(matching) == 1, 'RGB command not paired')
            command = matching[0]
            # The public runner writes results only after checking subprocess
            # returncode. Preserve the wrapper source card and derive each
            # successful exit from its checked DONE record, rather than adding
            # fabricated per-command fields to its original manifest.
            require('DONE ' + command['stem'] + ':' in wrapper_text and OPTOUT not in command['environment'],
                    'RGB checked process completion/optout differs')
            argv = command['command']
            for option, value in (('--backend', 'wgpu'), ('--warmup', '0'), ('--frames', '7'), ('--animation-step', '100'),
                                  ('--capture-frames', '0,1,2,3,4,5,6'), ('--size', '1024')):
                require(argv.count(option) == 1 and argv[argv.index(option) + 1] == value, 'RGB protocol differs: ' + option)
            log = inside(directory, record['log']).read_text()
            records = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in log.splitlines() if line.startswith('capture ')]
            require(len(records) == 7 and {int(item['logical_frame']) for item in records} == set(range(0, 601, 100)), 'RGB capture frames missing')
            for item in records:
                key = (side, scene, record['case'], int(item['logical_frame']))
                require(key not in captures, 'Duplicate RGB capture')
                captures[key] = item
    require(rgb.get('processes') == 14 and rgb.get('produced_ppm_files') == 98 and rgb.get('pairs') == len(rgb['results']) == 49 and
            rgb.get('all_rgb_identical') is True and rgb.get('rgb_pixels_recomputed') is True, 'RGB pixel-check result incomplete')
    for result in rgb['results']:
        require(result['rgb_mae'] == result['max_channel_error'] == result['pixels_different'] == result['pixels_over3'] == 0, 'RGB pair mismatch')
        for side in ('before', 'after'):
            capture = captures[side, result['scene_label'], result['case'], result['logical_frame']]
            require(capture['rgb_fnv64'] == result[side]['rgb_fnv64'] and capture['rgba_fnv64'] == result[side]['rgba_fnv64'] and
                    capture['state_fnv64'] == result['state_fnv64'], 'Capture checksum/state differs from RGB result')
            require(valid_sha(result[side]['ppm_sha256']), 'Captured pixel SHA missing')
            if require_pixels:
                path = Path(result[side]['original_image'])
                require(path.is_file() and sha(path) == result[side]['ppm_sha256'], 'Original pixel hash differs')
        require(all(result['before'][key] == result['after'][key] for key in ('ppm_sha256', 'rgb_fnv64', 'rgba_fnv64')), 'Before/after RGB bytes differ')
    require(len(captures) == 98, 'RGB complete capture coverage differs')
    return rgb


def cross_rgb_check(directory, control_command_path, control_log_path, analysis_command_path, analysis_log_path, require_pixels=False):
    controls = read(control_command_path)
    analysis = read(analysis_command_path)
    require(controls['exit_code'] == analysis['exit_code'] == 0 and
            controls['source_content_revisions'] == {'coingl': CONTROL, 'bgfx-vulkan': BASELINE, 'bgfx-opengl': BASELINE} and
            sha(control_log_path) == controls['log_sha256'] and sha(analysis_log_path) == analysis['log_sha256'],
            'Cross RGB execution provenance differs')
    require('Completed 12 new processes; raw CSV and logs preserved' in Path(control_log_path).read_text(), 'Cross RGB controls incomplete')
    rows = read(directory / 'results.json')
    comparisons = read(directory / 'rgb.json')
    images = read(directory / 'image-manifest.json')
    motion = read(directory / 'motion.json')
    copies = read(directory / 'copied-files.json')
    cases = {'materials-10', 'materials-100', 'transforms-10', 'static'}
    variants = {'coingl', 'bgfx-vulkan', 'bgfx-opengl', 'wgpu-vulkan'}
    require(len(rows) == len(motion) == 16 and {(row['case'], row['variant']) for row in rows} ==
            {(case, variant) for case in cases for variant in variants}, 'Cross RGB aggregate process coverage differs')
    require(copies['kind'] == 'Derived aggregation; no new renderer process or image', 'Cross RGB copied aggregation missing')
    for copied in copies['files']:
        if Path(copied['destination']).suffix.lower() != '.ppm':
            require(sha(inside(directory, copied['destination'])) == copied['sha256'], 'Cross RGB copied raw log/CSV differs')
    captures = {}
    for row in rows:
        text = inside(directory, row['log']).read_text()
        found = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in text.splitlines() if line.startswith('capture ')]
        require(len(found) == 7 and {int(item['logical_frame']) for item in found} == set(range(0, 601, 100)), 'Cross RGB sampled captures incomplete')
        for item in found:
            captures[row['case'], row['variant'], int(item['logical_frame'])] = item
    image_cards = {(record['case'], record['variant'], record['logical_frame']): record for record in images}
    result_cards = {(record['case'], record['variant'], record['logical_frame']): record for record in comparisons}
    require(len(captures) == len(images) == len(image_cards) == len(comparisons) == len(result_cards) == 112 and
            set(captures) == set(image_cards) == set(result_cards), 'Cross RGB image/result coverage differs')
    meaningful, trivial = [], []
    if require_pixels:
        import numpy as np
        from PIL import Image
    for key, capture in captures.items():
        case, variant, frame = key
        image, result = image_cards[key], result_cards[key]
        reference = captures[case, 'coingl', frame]
        require(capture['rgb_fnv64'] == image['rgb_fnv64'] and capture['rgba_fnv64'] == image['rgba_fnv64'] and
                capture['state_fnv64'] == reference['state_fnv64'] == result['state'] and result['reference'] == 'coingl' and
                valid_sha(image['ppm_sha256']), 'Cross RGB state/hash/reference differs')
        require(all(isinstance(result[field], (int, float)) and result[field] >= 0 for field in
                    ('rgb_mae', 'max_channel_error', 'pixels_different', 'pixels_over3')), 'Cross RGB metrics malformed')
        if variant == 'coingl':
            require(all(result[field] == 0 for field in ('rgb_mae', 'max_channel_error', 'pixels_different', 'pixels_over3')), 'CoinGL self-comparison differs')
            trivial.append(result)
        else:
            meaningful.append(result)
        if require_pixels:
            def pixels(record, identity):
                # The cross dataset is a log/CSV aggregation. Its PPMs remain
                # in the original controls or WG-after directories recorded by
                # captures/copied-files; aggregation creates no new image.
                path = Path(captures[identity]['image'])
                allowed = {(Path(original) / 'images').resolve() for original in copies['original_directories']}
                require(path.is_absolute() and path.resolve().parent in allowed and path.name == record['filename'],
                        'Cross RGB original pixel identity differs')
                candidates = [parent / record['filename'] for parent in allowed if (parent / record['filename']).is_file()]
                require(len(candidates) == 1 and candidates[0].resolve() == path.resolve(),
                        'Cross RGB original pixel must exist in exactly one recorded dataset')
                require(sha(path) == record['ppm_sha256'], 'Cross RGB pixel hash differs')
                return np.asarray(Image.open(path), dtype=np.int16)
            observed = pixels(image, key)
            expected = pixels(image_cards[case, 'coingl', frame], (case, 'coingl', frame))
            require(observed.shape == expected.shape, 'Cross RGB dimensions differ')
            error = np.abs(observed - expected)
            require(result['rgb_mae'] == float(error.mean()) and result['max_channel_error'] == int(error.max()) and
                    result['pixels_different'] == int(np.any(error != 0, axis=2).sum()) and
                    result['pixels_over3'] == int(np.any(error > 3, axis=2).sum()), 'Cross RGB metrics differ from pixels')
    require(len(meaningful) == 84 and len(trivial) == 28, 'Cross RGB self/experimental comparison counts differ')
    for row in motion:
        expected = 1 if row['case'] == 'static' else None
        require((row['distinct_states'] == row['distinct_rgb'] == 1) if expected else
                (row['distinct_states'] > 1 and row['distinct_rgb'] > 1), 'Cross RGB motion/static evidence differs')
    maxima = {variant: {key: max(record[key] for record in meaningful if record['variant'] == variant) for key in
              ('rgb_mae', 'max_channel_error', 'pixels_different', 'pixels_over3')}
              for variant in sorted(variants - {'coingl'})}
    return {'new_control_processes': 12, 'new_control_ppm_files': 84, 'reused_wgpu_processes': 4,
            'reused_wgpu_ppm_files': 28, 'experimental_comparisons': 84, 'self_comparisons': 28,
            'maxima': maxima, 'results_sha256': sha(directory / 'rgb.json'), 'captured_hashes_checked': 112}


def bgfx_diagnostic_check(directory, results_path, analyzer_path, parser_path):
    helper = module('material_bgfx_supplement_rederive', analyzer_path)
    stored = read(results_path)
    actual = helper.analyze(directory, parser_path)
    original = Path(stored['runs'][next(iter(stored['runs']))]['csv_input']['path']).parent
    replacements = [(str(directory.resolve()), '<bgfxdiagnostic>'), (str(original), '<bgfxdiagnostic>')]
    require(normalize(actual, replacements) == normalize(stored, replacements), 'BGFX diagnostic raw recomputation differs')
    require(actual['unique_counts'] == {'processes': 18, 'measured_frames': 270, 'warmup_frames': 90} and
            actual['complete_and_comparable'] is True and not actual['issues'], 'BGFX diagnostic incomplete')
    waits, signatures = {}, {}
    for variant in ('bgfx-vulkan', 'bgfx-opengl'):
        frames = [run['frames'][index] for run in actual['runs'].values() if run['command']['variant'] == variant for index in run['measured_row_indices']]
        require(len(frames) == 90, 'BGFX diagnostic measured frames differ')
        values = [frame['metrics']['bgfx.read_wait_ms'] for frame in frames]
        waits[variant] = {'minimum': min(values), 'maximum': max(values)}
        for frame in frames:
            scopes = {event['scope']: event['fields'] for event in frame['events']}
            require(scopes['bgfx_geometry']['instancing'] == 1 and scopes['bgfx_geometry']['instances'] == 40001 and
                    scopes['bgfx']['vertices'] == 24 and scopes['bgfx']['draws'] == 1 and
                    scopes['bgfx']['geometry_buffer_reused'] == 1, 'BGFX diagnostic signature changed')
        signatures[variant] = {'frames': 90, 'instancing': 1, 'instances': 40001, 'vertices': 24, 'draws': 1, 'geometry_buffer_reused': 1}
    return {'counts': actual['unique_counts'], 'raw_recomputed': True,
            'comparisons': actual['comparisons'], 'read_wait_ranges_ms': waits, 'signatures': signatures,
            'results_sha256': sha(results_path)}


def gate_check(directory):
    """Keep the actual mixed compiled-source map and registered CTest definitions."""
    manifest = read(directory / 'commands.json')
    source_map = {'wgpu': CURRENT, 'bgfx': BASELINE}
    require(manifest.get('source_content_revision_kind') == 'mixed' and
            manifest.get('source_content_revisions_by_backend') == source_map and
            manifest.get('source_snapshot_revision') == CURRENT,
            'Gate compiled sources differ')
    hashes = manifest['source_files_sha256']
    require(hashes and all(valid_sha(value) for value in hashes.values()) and
            manifest.get('source_revision_basis') == 'owner supplied compiled content by backend; source hashes checked against the checkout snapshot for mutation',
            'Gate source-file proof missing')
    pattern = re.compile(r'^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.{2,}\s*(.*?)\s+[\d.]+\s+sec\s*$', re.M)
    commands = manifest['commands']
    require(len({record['name'] for record in commands}) == len(commands), 'Duplicate gate command')
    categories, passed, definitions_card = Counter(), 0, {}
    for record in commands:
        name, definitions = record['name'], record['ctest_definitions']
        require('/' not in name and name not in ('.', '..') and definitions, 'Unsafe/incomplete gate')
        family = 'wgpu' if record['variant'].startswith('wgpu') else 'bgfx'
        require(record.get('source_content_revision') == source_map[family] and
                record.get('source_revision_after') == CURRENT and record.get('source_files_sha256_after') == hashes,
                'Gate source changed: ' + name)
        require(record.get('exit_code') == 0 and record.get('skip_observed') is False and
                not record.get('timeout') and not record.get('timed_out') and OPTOUT not in record['environment'],
                'Gate failed/skipped/timed out/opted out: ' + name)
        names = {definition['name'] for definition in definitions}
        require(len(names) == len(definitions) == record['expected_executions'], 'Gate count differs')
        executables = {definition['command'][0] for definition in definitions}
        require(set(record['executable_sha256']) == executables and
                all(valid_sha(value) for value in record['executable_sha256'].values()), 'Gate executable proof differs')
        inventories = [(build, card) for build, card in manifest['ctest_inventory'].items()
                       if all(definition in card['definitions'] for definition in definitions)]
        require(len(inventories) == 1, 'Gate definitions missing in CTest inventory')
        log = (directory / (name + '.log')).read_text()
        require(not re.search(r'\[SKIP\]|\bSkipped\b|\bNot Run\b', log, re.I), 'Skip appears in gate log')
        direct = record['direct_cli_override']
        if direct:
            require(len(definitions) == 1 and record['command'][0] == definitions[0]['command'][0], 'Direct gate executable differs')
            marker = record.get('required_output_marker')
            require(marker and marker in log, 'Direct gate success marker missing')
            count = 1
        else:
            argv = record['command']
            require(argv[0] == 'ctest' and '--test-dir' in argv and '-R' in argv, 'Incomplete CTest invocation')
            require(argv[argv.index('--test-dir') + 1] == inventories[0][0], 'CTest build differs')
            selection = argv[argv.index('-R') + 1]
            require({entry['name'] for entry in inventories[0][1]['definitions'] if re.search(selection, entry['name'])} == names,
                    'CTest selection differs')
            results = pattern.findall(log)
            require(len(results) == len(definitions) and {test for test, _ in results} == names and
                    all(status == 'Passed' for _, status in results), 'CTest result names/status differ')
            summary = re.search(r'(\d+)% tests passed, (\d+) tests failed out of (\d+)', log)
            require(summary and tuple(map(int, summary.groups())) == (100, 0, len(definitions)), 'CTest summary differs')
            full_log = (directory / (name + '-ctest.log')).read_text()
            blocks = re.split(r'^\d+/\d+ Testing:\s*', full_log, flags=re.M)[1:]
            require(len(blocks) == len(definitions) and {block.splitlines()[0].strip() for block in blocks} == names and
                    all(re.findall(r'^Test (Passed|Failed)\.$', block, re.M) == ['Passed'] for block in blocks),
                    'CTest full log disagrees')
            count = len(results)
        require(record.get('passed') is True and record.get('observed_passed_executions') == count, 'Gate recorded count differs')
        passed += count
        categories[record['category']] += count
        definitions_card[name] = {'category': record['category'], 'variant': record['variant'],
                                 'tests': sorted(names), 'direct_cli_override': direct,
                                 'source_content_revision': source_map[family]}
    require(manifest.get('complete') is True and passed == manifest.get('passed_gate_executions') ==
            manifest.get('expected_gate_executions') == 12 and
            dict(categories) == manifest.get('expected_categories') == {'core_cpu': 4, 'action_reuse': 2, 'gpu': 6},
            'Gate final counts differ')
    return {'executions': passed, 'passed': passed, 'failed': 0, 'skipped': 0,
            'registered_category_counts': dict(categories), 'commands': definitions_card,
            'category_note': 'core_cpu contém dois Core CPU e dois Core com integração GPU; Action/Reuse mistos.'}


def build_check(commands_path, paths_by_label):
    records = read(commands_path)
    require(len(records) == 2 and {record['label'] for record in records} == {'build-wgpu', 'cargo-cpu'}, 'Build/Cargo execution coverage differs')
    cards = {}
    for record in records:
        label = record['label']
        log = Path(paths_by_label[label])
        require(record['exit_code'] == 0 and record['source_content_revision_before'] == CURRENT ==
                record['source_content_revision_after'] and sha(log) == record['log_sha256'], 'Build/Cargo failed/source/hash differs')
        cards[label] = {'command': record['command'], 'exit_code': 0, 'log_sha256': sha(log)}
        if label == 'cargo-cpu':
            text = log.read_text()
            result = re.search(r'test result: ok\. (\d+) passed; (\d+) failed; (\d+) ignored; (\d+) measured; (\d+) filtered out;', text)
            require(result and tuple(map(int, result.groups()[1:])) == (0, 0, 0, 0), 'Cargo summary failed/ignored/missing')
            names = ['instancing::tests::material_and_instance_resources_compare_independent_owned_bytes',
                     'material_resource_tests::gpu_material_table_preserves_all_source_bits_and_zero_gpu_padding']
            require(all('test ' + name + ' ... ok' in text for name in names), 'New Cargo oracle did not pass')
            cards[label].update(tests_passed=int(result[1]), tests_failed=0, new_oracles=names)
    return cards


def binary_check(cards, post_path):
    require(set(cards) == {'baseline', 'after', 'coingl'}, 'Require three binary cards')
    snapshots = {name: read(path) for name, path in cards.items()}
    require({name: len(card['files']) for name, card in snapshots.items()} == {'baseline': 8, 'after': 8, 'coingl': 4}, 'Require20 pinned artifacts')
    require(snapshots['baseline']['source_content_revision'] == BASELINE and
            snapshots['after']['source_content_revision_kind'] == 'mixed' and
            snapshots['after']['source_content_revisions_by_backend'] == {'wgpu': CURRENT, 'bgfx': BASELINE} and
            snapshots['coingl']['source_content_revision'] == CONTROL, 'Binary compiled sources differ')
    expected = {}
    for name, card in snapshots.items():
        for record in card['files']:
            require(valid_sha(record['sha256']) and (('bytes' not in record and name == 'coingl') or
                    (type(record.get('bytes')) is int and record['bytes'] > 0)), 'Invalid artifact hash/size')
            key = (name, record['path'])
            require(key not in expected, 'Duplicate pinned artifact')
            expected[key] = record
            if name == 'after':
                require(record['source_content_revision'] == (CURRENT if record['variant_family'] == 'wgpu' else BASELINE), 'After artifact source differs')
    post = read(post_path)
    # The coordinator's post snapshot must provide stage/path identities, not just
    # twenty unlabelled hashes. Binary files are intentionally excluded here.
    records = post['files']
    aliases = {'final': 'after', 'current': 'after'}
    observed = {(aliases.get(record['stage'], record['stage']), record['path']): record for record in records}
    require(len(records) == len(observed) == 20 and set(observed) == set(expected), 'Post binary identity coverage differs')
    for key, before in expected.items():
        after = observed[key]
        require(after.get('unchanged') is True and type(after['bytes']) is int and after['bytes'] > 0 and
                ('bytes' not in before or after['bytes'] == before['bytes']) and
                after['sha256'] == after.get('post_sha256') == before['sha256'], 'Binary changed after campaigns')
    require(post.get('all_unchanged') is True, 'Post binary snapshot not complete')
    return {'artifacts': 20, 'all_unchanged': True}


def generator_tests_check(card_path, log_path, source_paths):
    card = read(card_path)
    require(card['exit_code'] == 0 and card['source_snapshot_revision'] == CURRENT and
            sha(log_path) == card['log_sha256'], 'Generator CPU tests source/exit/log differs')
    require(set(source_paths) == set(card['source_files_sha256']) and
            all(sha(source_paths[name]) == digest for name, digest in card['source_files_sha256'].items()),
            'Generator/test code hashes differ')
    log = Path(log_path).read_text()
    observed = re.search(r'^Ran (\d+) tests in [\d.]+s$', log, re.M)
    require(observed and int(observed[1]) == 6 and re.search(r'^OK$', log, re.M) and
            not re.search(r'^FAILED|\bskipped\b', log, re.M | re.I), 'Generator CPU summary failed/missing')
    return {'tests_passed': 6, 'tests_failed': 0, 'exit_code': 0, 'log_sha256': sha(log_path),
            'source_files_sha256': card['source_files_sha256']}


def binary_manifest_check(card_paths, manifests):
    """Tie measurement payloads to pins, including the libCoin.so.80 alias."""
    def canonical(path):
        return path + '.0.10' if path.endswith('/libCoin.so.80') else path
    expected = {}
    for path in card_paths.values():
        card = read(path)
        for record in card['files']:
            require(record['path'] not in expected, 'Binary cards have overlapping path identities')
            expected[record['path']] = record['sha256']
    checked = 0
    for manifest in manifests:
        payload = manifest['binary_hashes']
        records = payload.items() if isinstance(payload, dict) else ((record['path'], record['sha256']) for record in payload)
        for path, digest in records:
            require(expected.get(canonical(path)) == digest, 'Measurement binary differs from pinned cards: ' + path)
            checked += 1
    require(checked >= 16 + 3 + 4 * 3 + 9, 'Incomplete measurement binary identity proof')
    return checked


def scene_check(card_path, post_path, scene_paths, generator_path):
    card = read(card_path)
    require(len(card['scenes']) == 3 and len(scene_paths) == 4, 'Require original and three generated scenes')
    post = read(post_path)
    records = post['files']
    require(len(records) == 4 and post.get('all_unchanged') is True and
            {record['path'] for record in records} == set(scene_paths), 'Four post-campaign scene identities missing')
    expected = {record['scene']['path']: record['scene'] for record in card['scenes']}
    original = card['scenes'][0]['source_city']
    expected[original['path']] = original
    require(set(expected) == set(scene_paths), 'Scene cards differ from post snapshot')
    for record in records:
        path = scene_paths[record['path']]
        before = expected[record['path']]
        require(record.get('unchanged') is True and record['sha256'] == record['post_sha256'] == before['sha256'] == sha(path) and
                record['bytes'] == before['bytes'] == path.stat().st_size, 'Scene changed before/after campaigns')
    helper = module('material_scene_generator_readonly', generator_path)
    source_city = helper.parse_city(scene_paths[original['path']].read_bytes())
    for item in card['scenes']:
        require(item['generator']['sha256'] == sha(generator_path) and item['source_city'] == original,
                'Generator/input provenance differs')
        count, slots = item['parameters']['objects'], item['parameters']['material_slots']
        # Recreate serialization in memory: no generator filesystem mutation.
        palette = [helper.color_literal(helper.material_color(index)) for index in range(slots - 1)]
        pieces = [source_city['prefix']]
        for index, occurrence in enumerate(source_city['objects'][:count]):
            material = index % (slots - 1)
            pieces.append(' Separator {\n')
            pieces.append(f"  DEF CityMaterial{material} Material {{ diffuseColor {palette[material]} shininess 0.2 }}\n"
                          if index < slots - 1 else f"  USE CityMaterial{material}\n")
            pieces.append(occurrence['transform'] + occurrence['cube'] + ' }\n')
        pieces.append(source_city['suffix'])
        generated = ''.join(pieces).encode('ascii')
        require(hashlib.sha256(generated).hexdigest() == item['scene']['sha256'], 'Generated scene differs from preserved input')
        actual = helper.parse_city(generated)
        require(helper.layout_digest(source_city, count) == helper.layout_digest(actual, count) ==
                item['layout']['source_layout_sha256'] == item['layout']['output_layout_sha256'] and
                actual['source_palette_definitions'] == slots - 1 == actual['source_palette_unique_colors'],
                'Scene non-material layout/palette differs')
    return {'scenes': 4, 'all_unchanged': True, 'generated_scenes_rederived_in_memory': 3}


def inventory_check(evidence, repository):
    inventory = read(evidence / 'stage-files.json')
    require(inventory.get('inventory_excludes_itself') is True, 'Inventory must exclude itself')
    records = inventory['files']
    actual = {str(path.relative_to(repository)) for path in evidence.rglob('*')
              if path.is_file() and path.name != 'stage-files.json'} | {'docs/' + REPORT}
    expected = {record['path'] for record in records}
    require(len(expected) == len(records) and actual == expected, 'Inventory file set differs')
    for record in records:
        path = inside(repository, record['path'])
        require(path.stat().st_size == record['bytes'] and sha(path) == record['sha256'], 'Inventory hash differs')
    return len(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--repository', type=Path)
    args = parser.parse_args()
    evidence = args.stage.resolve()
    repository = args.repository.resolve() if args.repository else evidence.parents[2]
    collection = read(evidence / 'collection-inputs.json')
    metadata = read(evidence / 'stage-metadata.json')
    require(collection.get('ppm_files_copied') is False and collection.get('binaries_copied') is False and
            collection.get('runner_json_preserved_verbatim') is True, 'Archive exclusion/verbatim contract missing')
    for relative, record in collection['copied_files'].items():
        path = inside(evidence, relative)
        require(path.stat().st_size == record['bytes'] and sha(path) == record['sha256'], 'Original copy differs: ' + relative)
    quiet = quiet_check(evidence / 'quiet', evidence / 'study-campaign.py')
    diagnostic, cards = diagnostic_check(evidence / 'diagnostic', evidence / 'diagnostic-analysis.json', evidence / 'material-diagnostic.py')
    require(cards == metadata['diagnostic_cards'], 'Diagnostic admission cards differ after relocation')
    rgb_directories = {(side, scene): evidence / 'rgb' / (side + '-' + scene) for side in ('before', 'after') for scene in ('original', 'large')}
    wrappers = {side: evidence / 'execution' / ('rgb-' + side + '-commands.json') for side in ('before', 'after')}
    wrapper_logs = {(side, scene): evidence / 'execution' / ('rgb-' + side + '-' + scene + '.log')
                    for side in ('before', 'after') for scene in ('original', 'large')}
    rgb = rgb_check(rgb_directories, evidence / 'rgb/before-after.json', wrappers, wrapper_logs)
    cross = cross_rgb_check(evidence / 'rgb/cross-large', evidence / 'execution/rgb-controls-command.json',
                            evidence / 'execution/rgb-controls.log', evidence / 'execution/rgb-cross-analysis-command.json',
                            evidence / 'execution/rgb-cross-analysis.log')
    require(cross == metadata['rgb_cross'], 'Cross RGB summary differs after relocation')
    require(gate_check(evidence / 'gates') == metadata['gates'], 'Actual gates differ from report admission')
    builds = build_check(evidence / 'execution/build-commands.json', {label: evidence / 'execution' / (label + '.log') for label in ('build-wgpu', 'cargo-cpu')})
    require(builds == metadata['builds'], 'Build/Cargo admission differs')
    binaries = binary_check({name: evidence / ('binaries-' + name + '.json') for name in ('baseline', 'after', 'coingl')}, evidence / 'binary-hashes-post.json')
    require(binaries == metadata['binary_proof'], 'Binary post proof differs')
    measurement_manifests = [read(evidence / 'quiet/commands.json'), read(evidence / 'diagnostic/commands.json')]
    measurement_manifests += [read(path / 'manifest.json') for path in rgb_directories.values()]
    measurement_manifests.append(read(evidence / 'rgb/large-control-manifest.json'))
    binary_manifest_check({name: evidence / ('binaries-' + name + '.json') for name in ('baseline', 'after', 'coingl')}, measurement_manifests)
    scenes = scene_check(evidence / 'scenes/cards.json', evidence / 'scenes/post.json',
                         {path: inside(evidence, relative) for path, relative in collection['scene_files'].items()},
                         evidence / 'generate-material-slot-scene.py')
    require(scenes == metadata['scene_proof'], 'Scene in-memory reproduction/post proof differs')
    generator_card = read(evidence / 'execution/generator-tests.json')
    generator_tests = generator_tests_check(evidence / 'execution/generator-tests.json', evidence / 'execution/generator-tests.log',
                      {name: inside(evidence, 'source/' + name) for name in generator_card['source_files_sha256']})
    require(generator_tests == metadata['generator_tests'], 'Generator CPU tests admission differs')
    bgfx = bgfx_diagnostic_check(evidence / 'supplemental/bgfx-regression', evidence / 'supplemental/bgfx-regression-analysis.json',
                                evidence / 'supplemental/analyze-bgfx-regression.py', evidence / 'supplemental/parse-bgfx-regression.py')
    require(bgfx == metadata['bgfx_regression_diagnostic'], 'Separate BGFX diagnostic admission differs')
    reporter = module('material_report_rederive', evidence / 'plot-and-report.py')
    data = reporter.validate(metadata, quiet, diagnostic, diagnostic, rgb)
    report_inputs = read(evidence / 'report-inputs.json')
    expected_inputs = {'metadata': evidence / 'stage-metadata.json', 'quiet': evidence / 'quiet/results.json',
                       'diagnostic-primary': evidence / 'diagnostic-analysis.json', 'diagnostic-dimensional': evidence / 'diagnostic-analysis.json',
                       'rgb': evidence / 'rgb/before-after.json'}
    require(set(report_inputs['inputs']) == set(expected_inputs) and report_inputs['reporter_sha256'] == sha(evidence / 'plot-and-report.py'),
            'Report input/script coverage differs')
    for name, path in expected_inputs.items():
        record = report_inputs['inputs'][name]
        require(path.stat().st_size == record['bytes'] and sha(path) == record['sha256'], 'Report input hash differs')
    document = repository / 'docs' / REPORT
    generated = reporter.report(metadata, quiet, diagnostic, diagnostic, rgb, data, report_inputs['figure_reference'])
    require(document.read_text() == generated, 'Markdown rederivation differs byte for byte')
    for name, record in report_inputs['outputs'].items():
        path = document if name == REPORT else inside(evidence, name)
        require(path.stat().st_size == record['bytes'] and sha(path) == record['sha256'], 'Report/figure output hash differs')
    count = inventory_check(evidence, repository)
    print(json.dumps({'complete': True, 'quiet': quiet['completed_unique_counts'], 'diagnostic_proofs': cards,
                      'gates': 12, 'cargo_tests': builds['cargo-cpu']['tests_passed'], 'rgb_pairs': 49,
                      'generator_cpu_tests': generator_tests['tests_passed'],
                      'rgb_cross_experimental_pairs': 84, 'artifacts_unchanged': 20, 'scenes_unchanged': 4,
                      'bgfx_diagnostic_processes': 18, 'inventory_files': count, 'markdown_byte_identical': True,
                      'pixel_scope': 'Raw captures/hashes/results checked; excluded PPMs were compared before collection'}, indent=2))


if __name__ == '__main__':
    main()
