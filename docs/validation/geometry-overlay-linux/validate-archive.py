#!/usr/bin/env python3
"""Validate a relocated geometry-overlay archive without GPU/build/Git or writes."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
STAGE = 'geometry-overlay-linux'
REPORT = 'coin-render-geometry-overlay-linux.md'
FIGURE = 'validation/geometry-overlay-linux/geometry-overlay.png'
VARIANTS = ('coingl', 'bgfx-vulkan', 'bgfx-opengl', 'wgpu-vulkan')
BASELINE_RENDER = '9a594fa39c7ca7924f9931863d4bdd7a37165cee'
BASELINE_SNAPSHOT = 'a2e19d360db9c4ef187550ae1513fddd0585a371'
CURRENT = '96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33'
CONTROL = '4d63bb993022ee8d40802558b0871a4803002b8d'
CASES = {'materials-10','geometry-10','geometry-100','transforms-10'}
EXPECTED_COUNTS = {'steady': {'processes':84,'measured_frames':1260,'warmup_frames':420},
                   'ablation': {'processes':36,'measured_frames':252,'warmup_frames':108}}
OPTOUT = 'COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION'
PROBE_COUNTS = {'processes':6,'measured_frames':90,'warmup_frames':30}


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


def gate_check(directory, metadata, specification, expected_status=None, expected_source=None):
    manifest = read(directory/'commands.json')
    source_hashes=manifest.get('source_files_sha256')
    require(source_hashes and all(valid_sha(digest) for digest in source_hashes.values()) and
            manifest.get('source_revision_basis')=='owner supplied compiled content; source files checked for mutation',
            'Owner supplied source-file proof is missing')
    commands = manifest['commands']
    require(isinstance(specification, dict) and specification, 'Gate command specification missing')
    expected = {name: set(item['tests']) for name, item in specification.items()}
    require(all(item.get('category') and len(item['tests']) == len(expected[name]) and expected[name]
                for name, item in specification.items()), 'Gate category/test list incomplete or duplicated')
    require({record['name'] for record in commands} == set(expected) and len(commands) == len(expected), 'Gate command coverage differs')
    revision = expected_source or metadata['source_content_revision']
    require(valid_sha(manifest['source_content_revision'], 40) and manifest['source_content_revision'] == revision, 'Gate source revision differs')
    statuses, categories = Counter(), Counter()
    pattern = re.compile(r'^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.{2,}\s*(.*?)\s+[\d.]+\s+sec\s*$', re.M)
    for record in commands:
        name, spec = record['name'], specification[record['name']]
        require('/' not in name and name not in ('.', '..'), 'Unsafe gate command name')
        definitions = record['ctest_definitions']
        require({test['name'] for test in definitions} == expected[name] and len(definitions) == len(expected[name]), 'CTest definitions differ: '+name)
        require(record.get('expected_executions') == len(definitions), 'Declared gate execution count differs: '+name)
        require(record.get('category') == spec.get('source_category'), 'Gate category differs: '+name)
        require(record.get('variant') == spec.get('variant'), 'Gate rendering variant differs: '+name)
        require(record.get('skip_observed') is False and not record.get('timeout') and not record.get('timed_out'), 'Gate skip/timeout missing or observed: '+name)
        require(record.get('source_revision_after') == revision, 'Gate source changed or post-check missing: '+name)
        require(record.get('source_files_sha256_after')==source_hashes, 'Gate source files changed: '+name)
        require(OPTOUT not in record.get('environment', {}), 'Gate inherited composition optout: '+name)
        log = (directory/(name+'.log')).read_text()
        require(re.search(r'\[SKIP\]|\bSkipped\b|\bNot Run\b', log, re.I) is None, 'Gate skip appears in log: '+name)
        registered_executables = {definition['command'][0] for definition in definitions}
        require(set(record.get('executable_sha256', {})) == registered_executables and
                all(Path(executable).is_absolute() and valid_sha(digest) for executable, digest in record['executable_sha256'].items()),
                'Gate executable SHA coverage invalid: '+name)
        inventory_matches = [(build, snapshot) for build, snapshot in manifest.get('ctest_inventory', {}).items()
                             if all(definition in snapshot.get('definitions', []) for definition in definitions)]
        require(len(inventory_matches) == 1, 'Gate definitions differ from registered CTest inventory: '+name)
        direct = spec.get('direct_cli_override') is True
        require(record.get('direct_cli_override') is direct, 'Gate execution mode differs: '+name)
        if direct:
            require(len(definitions) == 1 and spec.get('arguments'), 'Direct gate needs one registered definition and explicit override')
            expected_argv = [definitions[0]['command'][0]] + spec['arguments']
            require(record['command'] == expected_argv, 'Direct gate argv differs from registered executable + override: '+name)
            marker = spec.get('required_output_marker')
            require(record.get('required_output_marker') == marker, 'Direct gate marker contract differs: '+name)
            require(not marker or marker in log, 'Direct gate success marker missing: '+name)
            # A direct CLI execution produces no CTest status line/LastTest log.
            require(not (directory/(name+'-ctest.log')).exists(), 'Direct gate unexpectedly has CTest result artifact: '+name)
            passed = int(record.get('exit_code') == 0)
            failed = 1-passed
        else:
            command = record['command']
            require(command and command[0] == 'ctest' and '--test-dir' in command and '-R' in command and
                    '--output-on-failure' in command, 'CTest argv is incomplete: '+name)
            require(command[command.index('--test-dir')+1] == inventory_matches[0][0], 'CTest build differs from registered definitions: '+name)
            selected = command[command.index('-R')+1]
            require({definition['name'] for definition in inventory_matches[0][1]['definitions'] if re.search(selected, definition['name'])} == expected[name],
                    'CTest selection regex differs: '+name)
            results = pattern.findall(log)
            require(len(results) == len(definitions) and {test for test, _ in results} == expected[name], 'CTest result coverage differs: '+name)
            require(all(status in ('Passed', '***Failed') for _, status in results), 'Unknown/skipped CTest status: '+name)
            passed = sum(status == 'Passed' for _, status in results)
            failed = len(results)-passed
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
        require((record.get('exit_code') == 0) == (failed == 0), 'Gate exit/status disagree: '+name)
        require(record.get('observed_passed_executions') == passed, 'Recorded/actual passed gate count differs: '+name)
        require(record.get('passed') is (failed == 0), 'Gate recorded outcome differs: '+name)
        statuses.update({'passed': passed, 'failed': failed, 'executions': len(definitions)})
        categories[spec['category']] += len(definitions)
    actual = dict(statuses)
    if expected_status is None:
        expected_status = {'passed': sum(len(value) for value in expected.values()), 'failed': 0,
                           'executions': sum(len(value) for value in expected.values())}
        require(manifest.get('complete') is True and manifest.get('passed_gate_executions') == expected_status['passed'],
                'Final gate completion/count is missing')
        require(manifest.get('expected_gate_executions') == expected_status['executions'], 'Gate expected count differs')
        source_categories = Counter()
        for name, spec in specification.items(): source_categories[spec['source_category']] += len(spec['tests'])
        require(manifest.get('expected_categories') == dict(source_categories), 'Gate expected category counts differ')
    require(actual == expected_status, 'Gate status/counts differ: '+str(actual)+' / '+str(expected_status))
    return {'status_counts': actual, 'category_counts': dict(categories),
            'direct_cli_execution_count': sum(item['expected_executions'] for item in commands if item['direct_cli_override'])}


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


def sources_check(manifest, side, metadata):
    expected = {variant: CONTROL if variant == 'coingl' else BASELINE_RENDER if side == 'before' else CURRENT
                for variant in VARIANTS}
    require(manifest.get('variant_source_content_revisions', manifest.get('variant_sources')) == expected,
            'Campaign source map differs: '+side)
    require(manifest.get('source_content_revision') == (BASELINE_RENDER if side == 'before' else CURRENT) and
            manifest.get('scene_sha256') == metadata['scene_sha256'], 'Campaign source/scene differs: '+side)
    for record in manifest['commands']:
        variant = record.get('variant') or next((v for v in VARIANTS if '-'+v+'-' in record.get('stem','')), None)
        require(variant in expected and record.get('source_content_revision') == expected[variant] and
                record.get('exit_code') == 0 and not record.get('timed_out') and
                OPTOUT not in record.get('environment', {}), 'Campaign command failed/source/optout differs')


def binary_check(snapshot_paths, post_path, manifests):
    snapshots = {stage: read(path) for stage,path in snapshot_paths.items()}
    require({stage:len(card['files']) for stage,card in snapshots.items()} == {'baseline':8,'final':8,'coingl':4},
            'Binary snapshot counts differ')
    sources = {'baseline':BASELINE_RENDER,'final':CURRENT,'coingl':CONTROL}
    require(all(card['source_content_revision'] == sources[stage] for stage,card in snapshots.items()) and
            snapshots['baseline']['source_snapshot_revision'] == BASELINE_SNAPSHOT,
            'Binary source/snapshot cards differ')
    post = read(post_path);records=post['files'];verified={};counts=Counter()
    require(post.get('all_unchanged') is True and len(records) == len({item['path'] for item in records}) == 20,
            'Post-campaign 20 hashes incomplete')
    for item in records:
        stage=item['stage'];require(stage in snapshots, 'Unknown binary stage')
        originals=[entry for entry in snapshots[stage]['files'] if entry['path'] == item['path']]
        require(len(originals)==1, 'Post hash missing from original snapshot')
        original=originals[0]
        require(valid_sha(item['sha256']) and original['sha256'] == item['sha256'] == item.get('post_sha256') and
                item.get('unchanged') is True and original['role'] == item['role'] and
                original.get('source_content_revision', snapshots[stage]['source_content_revision']) == sources[stage],
                'Post binary source/role/hash differs')
        if 'bytes' in original: require(original['bytes'] == item.get('bytes'), 'Post binary size differs')
        verified[item['path']]=item['sha256'];counts[stage]+=1
    require(dict(counts)=={'baseline':8,'final':8,'coingl':4}, 'Post binary categories differ')
    for manifest in manifests:
        for path,digest in manifest['binary_hashes'].items():
            aliases=(path,path+'.0.10') if path.endswith('/libCoin.so.80') else (path,)
            require(any(verified.get(alias)==digest for alias in aliases), 'Campaign binary hash not covered by post20: '+path)
    return 20


def build_check(path, log_root=None):
    manifest=read(path);require(len(manifest['commands'])==2, 'Require both completed builds')
    families=set()
    for item in manifest['commands']:
        argv=item['command'];require(argv[:2]==['cmake','--build'] and item.get('exit_code')==0 and
            item.get('source_revision_at_start')==item.get('source_revision_at_end')==CURRENT,
            'Build exit/source differs')
        family='wgpu' if Path(argv[2]).name.endswith('-wgpu') else 'bgfx' if Path(argv[2]).name.endswith('-bgfx') else None
        require(family is not None and family not in families, 'Build family duplicate/unknown');families.add(family)
        log=(log_root/Path(item['log']).name) if log_root else Path(item['log'])
        text=log.read_text();require(text and not re.search(r'^FAILED:|ninja: build stopped|fatal error:',text,re.M), 'Build log failed/empty')
    require(families=={'wgpu','bgfx'}, 'Both build families are required')
    return 2


def bootstrap_check(directory, log_path, command_path, empty_recorded=False):
    # Git does not retain empty directories. A relocated checkout may use the
    # hashed collection manifest's explicit original-empty-directory record.
    require((directory.is_dir() and not any(directory.iterdir())) or (empty_recorded and not directory.exists()),
            'Bootstrap directory must record zero test artifacts')
    record=read(command_path)
    require(record.get('source_snapshot_revision')==CURRENT and len(record['commands'])==1,
            'Bootstrap source/command coverage differs')
    command=record['commands'][0]
    require(command.get('name')=='gates' and command.get('exit_code')==1 and
            command.get('source_revision_at_start')==command.get('source_revision_at_end')==CURRENT,
            'Bootstrap recorded outcome/source differs')
    text=log_path.read_text()
    require('FileNotFoundError' in text and 'testsuite/coinrender/CoinRenderActionTest.cpp' in text and
            not re.search(r'^START\s|^\s*\d+/\d+\s+Test\s+#',text,re.M), 'Bootstrap must stop before gate execution')
    return {'test_executions':0,'runner_exit_code':1,'cause':'source path typo before executing tests'}


def rgb_check(summary_directory, summary, datasets, open_pixels=False):
    rgb=read(summary_directory/'rgb-before-after.json');require(rgb==summary['rgb_before_after'], 'RGB summary differs')
    results=rgb['results'];require(rgb.get('all_rgb_identical') is True and rgb['comparison_count']==len(results)==112,
                                'RGB pairs/result incomplete')
    expected={(case,variant,1,frame*100) for case in CASES for variant in VARIANTS for frame in range(7)}
    identities={(item['case'],item['variant'],item['round'],item['logical_frame']) for item in results}
    require(len(identities)==len(results) and identities==expected, 'RGB logical-frame coverage differs')
    captures={side:capture_records(summary_directory/('verify-'+side),datasets['verify-'+side]) for side in ('before','after')}
    require(all(set(values)==expected for values in captures.values()), 'Capture-log coverage differs')
    originals=[]
    for item in results:
        identity=(item['case'],item['variant'],item['round'],item['logical_frame'])
        require(all(item[key]==0 for key in ('rgb_mae','max_channel_error','pixels_different','pixels_over3')) and
                valid_sha(item['before']['ppm_sha256']) and item['before']['ppm_sha256']==item['after']['ppm_sha256'],
                'RGB recorded errors/hash differs')
        pair=[]
        for side in ('before','after'):
            captured,stored=captures[side][identity],item[side]
            require(captured['state_fnv64']==item['state_fnv64'] and captured['rgb_fnv64']==stored['rgb_fnv64'] and
                captured['rgba_fnv64']==stored['rgba_fnv64'] and captured['image']==stored['original_image'],
                'Capture checksum/state/original reference differs')
            pair.append(Path(stored['original_image']))
        originals.append((item,pair))
    available=all(path.is_file() for _,pair in originals for path in pair);recomputed=False
    if open_pixels and available:
        from PIL import Image
        for item,pair in originals:
            pixels=[]
            for side,path in zip(('before','after'),pair):
                require(sha(path)==item[side]['ppm_sha256'], 'Original PPM hash differs')
                with Image.open(path) as image:
                    require(image.mode=='RGB', 'Original PPM is not RGB');pixels.append((image.size,image.tobytes()))
            require(pixels[0]==pixels[1], 'Original RGB pixels differ')
        recomputed=True
    return {'pairs_recorded':112,'processes':32,'produced_ppm_files':224,
            'original_ppms_available':available,'rgb_pixels_recomputed':recomputed}


def probe_check(directory, script_path, environment_helper_path, metadata):
    """Recompute the quiet same-build supplement; never launch its main()."""
    manifest=read(directory/'commands.json');summary=read(directory/'summary.json')
    require(manifest.get('source_content_revision')==CURRENT and manifest.get('scene_sha256')==metadata['scene_sha256'] and
            manifest.get('expected_unique_counts')==manifest.get('completed_unique_counts')==PROBE_COUNTS and
            manifest.get('complete_and_comparable') is True and manifest.get('binaries_unchanged') is True,
            'Quiet probe source/scene/counts/completion differs')
    require(manifest.get('binary_hashes')==manifest.get('binary_hashes_post_campaign') and
            len(manifest['binary_hashes'])==3 and all(valid_sha(value) for value in manifest['binary_hashes'].values()),
            'Quiet probe three binary hashes differ')
    require(manifest.get('runner_sha256')==manifest.get('parser_sha256')==sha(script_path) and
            manifest.get('environment_helper_sha256')==sha(environment_helper_path), 'Quiet probe helper provenance differs')
    parameters={'variant':'bgfx-vulkan','case':'geometry-10','rounds':3,'warmup':5,'frames':15,
                'size':1024,'gpu':'nvidia','transparency':'object','optout':OPTOUT,'trace':False}
    require(manifest['parameters']==parameters, 'Quiet probe protocol differs')
    records=manifest['commands'];expected={(round_number,mode) for round_number in range(1,4) for mode in ('on','off')}
    require(len(records)==6 and {(item['round'],item['mode']) for item in records}==expected,
            'Quiet probe six identities incomplete/duplicated')
    require([(item['round'],item['mode']) for item in records]==[(1,'on'),(1,'off'),(2,'off'),(2,'on'),(3,'on'),(3,'off')],
            'Quiet probe alternating pair order differs')
    helper=module('geometry_quiet_probe',script_path);recomputed=[]
    for record in records:
        stem='bgfx-vulkan-geometry-10-'+record['mode']+'-'+str(record['round'])
        require(record['stem']==stem and record['variant']=='bgfx-vulkan' and record['case']=='geometry-10' and
                record['source_content_revision']==CURRENT and record.get('exit_code')==0 and
                record.get('timed_out') is False and record.get('adapter_verified_nvidia') is True and
                record.get('trace_environment_absent') is True and record.get('phase_trace_seen') is False,
                'Quiet probe command outcome/identity differs: '+stem)
        command=record['command'];require(len(command)==19 and command[0] in manifest['binary_hashes'], 'Quiet probe argv/binary differs')
        expected_command=[command[0],'--backend','bgfx','--scene',command[4],'--animation','geometry','--animated-percent','10',
                          '--transparency','object','--size','1024','--warmup','5','--frames','15','--samples-output',command[-1]]
        require(command==expected_command and Path(command[-1]).name==stem+'.csv' and
                record['timed_command']==['/usr/bin/time','-f','benchmark_peak_rss_kib=%M',*command],
                'Quiet probe command protocol differs: '+stem)
        environment=record['environment']
        require(environment.get('COIN_BGFX_RENDERER')=='vulkan' and
                all(key not in environment for key in ('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES')) and
                ((record['mode']=='off' and environment.get(OPTOUT)=='1') or (record['mode']=='on' and OPTOUT not in environment)),
                'Quiet probe option/renderer/tracing environment differs: '+stem)
        log_path=inside(directory,stem+'.log');log=log_path.read_text()
        require(log==(inside(directory,stem+'.stdout').read_text()+inside(directory,stem+'.stderr').read_text()) and
                record['log_input']['sha256']==sha(log_path) and Path(record['log_input']['path']).name==log_path.name and
                'COIN_RENDER_PHASE ' not in log and 'adapter=BGFX Vulkan NVIDIA' in log and
                'backend=bgfx size=1024x1024 warmup=5' in log and 'scene_update=geometry' in log,
                'Quiet probe raw log/hash/adapter/trace differs: '+stem)
        csv_path=inside(directory,stem+'.csv');samples=helper.samples(csv_path)
        replacements=[(str(Path(record['samples']['csv_input']['path']).parent),'<probe>'),(str(directory.resolve()),'<probe>')]
        require(normalize(samples,replacements)==normalize(record['samples'],replacements), 'Quiet probe CSV recomputation differs: '+stem)
        updated=dict(record);updated['samples']=samples;recomputed.append(updated)
    derived=helper.summarize(recomputed)
    require(derived==summary==manifest['summary'] and derived['complete_and_comparable'] is True and
            len(derived['pairs'])==3 and all(group['processes']==3 for group in derived['groups'].values()),
            'Quiet probe aggregate/pairs/ranges recomputation differs')
    focused={'source_content_revision':CURRENT,'scene_sha256':metadata['scene_sha256'],'parameters':parameters,
             'unique_counts':PROBE_COUNTS,'complete_and_comparable':True,'summary':summary,
             'evidence_reference':'validation/geometry-overlay-linux/regression-probe/summary.json'}
    if 'focused_probe' in metadata:require(metadata['focused_probe']==focused, 'Quiet probe report metadata differs')
    return focused,manifest


def data_check(summary_directory, analysis_path, trace_path, analyzer_path, diagnostic_directory,
               metadata, gate_directory, snapshot_paths, post_path, builds_path, build_log_root=None, pixels=False,
               extra_binary_manifests=()):
    require(metadata['source_content_revision']==CURRENT and metadata['baseline_source_content_revision']==BASELINE_RENDER and
            metadata['baseline_source_snapshot_revision']==BASELINE_SNAPSHOT and metadata['coingl_source_content_revision']==CONTROL and
            metadata['expected_counts']==EXPECTED_COUNTS, 'Stage source/protocol metadata differs')
    summary=read(summary_path(summary_directory));stored=read(analysis_path)
    require(set(stored['campaigns'])=={'steady'} and len(stored['diagnostics'])==1 and
            stored['unique_counts']==EXPECTED_COUNTS['steady'] and stored['campaign_counts_match_expected'] is True,
            'Analysis coverage/aggregate counts differ')
    require(stored['analysis_metadata']['script_sha256']==sha(analyzer_path) and
            stored['analysis_metadata']['trace_helper_sha256']==sha(trace_path), 'Analysis helper provenance differs')
    trace=module('geometry_archive_trace',trace_path);analyzer=module('geometry_archive_analysis',analyzer_path)
    original=stored['campaigns']['steady'];diagnostic=next(iter(stored['diagnostics'].values()))
    replacements=[]
    for side in ('before','after'):
        replacements.extend([(original[side]['directory'],'<'+side+'>'),
                             (str((summary_directory/side).resolve()),'<'+side+'>')])
    replacements.extend([(diagnostic['metadata_and_raw_trace']['input_directory'],'<diagnostic>'),
                         (str(diagnostic_directory.resolve()),'<diagnostic>')])
    replacements=sorted(set(replacements),key=lambda pair:len(pair[0]),reverse=True)
    campaign=analyzer.compare_campaigns(trace,'steady',summary_directory/'before',summary_directory/'after')
    require(normalize(campaign,replacements)==normalize(original,replacements) and
            campaign['complete_and_comparable'] is True and campaign['unique_counts']==EXPECTED_COUNTS['steady'] and
            set(campaign['comparisons'])=={case+'|'+variant for case in CASES for variant in VARIANTS},
            'Matched raw recomputation/protocol differs')
    core=module('geometry_summary_core',summary_directory/'campaign_core.py')
    summarizer=module('geometry_summarizer',summary_directory/'summarize.py')
    before=core.summarize_campaign('before',summary_directory/'before')
    after=core.summarize_campaign('after',summary_directory/'after')
    require(summarizer.compare_timing(core,before,after)==summary['timing'], 'Summary CSV/log recomputation differs')
    manifests=[]
    for side,result in (('before',before),('after',after)):
        require(result['groups']==summary['campaigns'][side]['groups'], 'Summary groups differ')
        sources_check(result['manifest'],side,metadata);manifests.append(result['manifest'])
        parameters=result['manifest']['parameters']
        require(all(int(parameters[key])==value for key,value in (('rounds',3),('warmup',5),('frames',15))), 'Matched sample protocol differs')
    require(all(item.get('counted_once') is True for item in campaign['shared_controls']), 'Shared CoinGL deduplication unproved')
    datasets={}
    for side in ('before','after'):
        label='verify-'+side;result=core.summarize_campaign(label,summary_directory/label)
        require(result['groups']==summary['campaigns'][label]['groups'] and len(result['processes'])==len(result['groups'])==16 and
                sum(item['measured_frames'] for item in result['processes'])==112 and
                sum(item['warmup_frames'] for item in result['processes'])==0, 'Verification 16/112/0 coverage differs')
        sources_check(result['manifest'],side,metadata);manifests.append(result['manifest']);datasets[label]=result
    contract=metadata['diagnostic_contract'];require(contract.get('configured') is True, 'Geometry contract not admitted')
    observed=analyzer.read_diagnostic(trace,diagnostic_directory,contract)
    require(normalize(observed,replacements)==normalize(diagnostic,replacements) and observed['contract']==contract and
            observed['unique_counts']==EXPECTED_COUNTS['ablation'] and observed['complete_and_comparable'] is True and
            observed['contract_observation_passed'] is True, 'Aligned diagnostic recomputation/proofs/counts differ')
    require(stored['diagnostic_contract_input']['metadata']==contract, 'Diagnostic contract provenance differs')
    diag_manifest=observed['metadata_and_raw_trace']['command_metadata']
    require(diag_manifest['source_content_revision']==CURRENT and diag_manifest['scene_sha256']==metadata['scene_sha256'], 'Ablation source/scene differs')
    require(len(observed['metadata_and_raw_trace']['runs'])==36 and
            all(run['geometry_contract_passed'] and run['geometry_validation_alignment']['passed']
                for run in observed['metadata_and_raw_trace']['runs'].values()), 'Ablation36 action-alignment proofs differ')
    manifests.append(diag_manifest)
    gates=gate_check(gate_directory,metadata,metadata['gate_commands'])
    require(gates['status_counts']=={'executions':12,'passed':12,'failed':0} and gates['category_counts']==metadata['gate_counts'], 'Final12 gates differ')
    facts={'matched':campaign['unique_counts'],'ablation':observed['unique_counts'],
           'rgb':rgb_check(summary_directory,summary,datasets,pixels),'gates':gates,
           'builds':build_check(builds_path,build_log_root),'post_binary_hashes':binary_check(snapshot_paths,post_path,manifests+list(extra_binary_manifests))}
    return facts,stored,summary


def validate(evidence, repository, pixels=False):
    inventory_count=inventory_check(evidence,repository);metadata=read(evidence/'stage-metadata.json')
    copied=copied_files_check(evidence/'steady')
    focused,probe_manifest=probe_check(evidence/'regression-probe',evidence/'regression-probe.py',
                                     evidence/'execution/benchmark-runner.py',metadata)
    require(metadata.get('focused_probe')==focused, 'Quiet probe metadata missing')
    facts,analysis,summary=data_check(evidence/'steady',evidence/'analysis.json',evidence/'diagnostic/derive-diagnostic.py',
        evidence/'analyze-geometry-overlay.py',evidence/'diagnostic',metadata,evidence/'gates',
        {stage:evidence/name for stage,name in (('baseline','binaries-before.json'),('final','binaries-after.json'),('coingl','binaries-coingl.json'))},
        evidence/'binary-hashes-post-campaign.json',evidence/'build-commands.json',evidence/'builds',pixels,[probe_manifest])
    facts['focused_probe']={'counts':focused['unique_counts'],'total_ms':focused['summary']['comparisons']['total_ms'],
                            'pairs_preserved':3,'raw_csv_log_recomputation_identical':True}
    require(read(evidence/'hardware-after-main.json').get('commands'), 'Hardware snapshot before quiet probe missing')
    require(read(evidence/'diagnostic-contract.json')==metadata['diagnostic_contract'], 'Standalone diagnostic contract differs')
    final_observation=read(evidence/'diagnostic-observation.json')
    initial_observation=read(evidence/'diagnostic-observation-initial.json')
    require(final_observation['diagnostics']==analysis['diagnostics'] and
            final_observation['diagnostic_contract_input']['metadata']==metadata['diagnostic_contract'] and
            final_observation['diagnostic_contract_input']['input']['sha256']==sha(evidence/'diagnostic-contract.json'),
            'Final admitted observation provenance differs')
    require(not initial_observation['campaigns'] and len(initial_observation['diagnostics'])==1 and
            initial_observation['diagnostic_contract_input']['metadata']['configured'] is False,
            'Initial pending-contract observation history differs')
    initial=next(iter(initial_observation['diagnostics'].values()))
    require(initial['unique_counts']==EXPECTED_COUNTS['ablation'] and initial['contract_observation_passed'] is True,
            'Initial observed diagnostic counts/proof differs')
    collection=read(evidence/'collection-inputs.json')
    facts['bootstrap']=bootstrap_check(evidence/'history/gates-bootstrap-initial',
        evidence/'history/gates-run.log-bootstrap-initial',evidence/'history/execution-commands.json-bootstrap-initial',
        'history/gates-bootstrap-initial' in collection['empty_directories'])
    reporter=module('geometry_report_archive',evidence/'plot-and-report.py')
    data=reporter.validate(metadata,analysis,summary)
    require(reporter.report(metadata,analysis,data,FIGURE)==(repository/'docs'/REPORT).read_text(), 'Markdown regeneration differs')
    inputs=read(evidence/'report-inputs.json')
    require(set(inputs['inputs'])=={'summary','analysis','metadata'} and
            set(inputs['outputs'])=={REPORT,'geometry-overlay.png','geometry-overlay.svg'}, 'Report provenance coverage differs')
    for key,path in (('summary',summary_path(evidence/'steady')),('analysis',evidence/'analysis.json'),('metadata',evidence/'stage-metadata.json')):
        require(inputs['inputs'][key]['sha256']==sha(path), 'Report input hash differs: '+key)
    require(inputs['generator']['sha256']==sha(evidence/'plot-and-report.py'), 'Report generator hash differs')
    for name,digest in inputs['outputs'].items():
        path=repository/'docs'/name if name.endswith('.md') else inside(evidence,name)
        require(sha(path)==digest, 'Report/figure output hash differs: '+name)
    facts.update(stage_files_verified=inventory_count,copied_files_verified=copied,report_regeneration_identical=True)
    return facts


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',type=Path,default=Path(__file__).resolve().parent)
    parser.add_argument('--repository-root',type=Path)
    parser.add_argument('--recompute-rgb-if-available',action='store_true')
    args=parser.parse_args();evidence=args.evidence.resolve()
    repository=args.repository_root.resolve() if args.repository_root else evidence.parents[2]
    require(evidence.is_relative_to(repository) and evidence.is_dir(), 'Invalid archive layout')
    print(json.dumps(validate(evidence,repository,args.recompute_rgb_if_available),indent=2,allow_nan=False))


if __name__=='__main__': main()
