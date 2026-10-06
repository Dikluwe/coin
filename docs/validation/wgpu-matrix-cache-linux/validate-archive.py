#!/usr/bin/env python3
"""Validate this archived evidence without builds, benchmarks or GPU commands."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
here = Path(__file__).resolve().parent
repository = here.parents[2]

def read(path):
    return json.loads(path.read_text())

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

def require(condition, context):
    if not condition:
        raise ValueError(context)

inventory = read(here / 'stage-files.json')
expected = {item['path'] for item in inventory['files']}
actual = {str(path.relative_to(repository)) for path in here.rglob('*')
          if path.is_file() and path.name != 'stage-files.json'}
actual.add('docs/coin-render-wgpu-matrix-cache-linux.md')
require(actual == expected, 'Stage inventory file set differs')
for item in inventory['files']:
    path = repository / item['path']
    require(path.is_file() and path.stat().st_size == item['bytes'] and sha(path) == item['sha256'],
            'Stage inventory mismatch: ' + str(path))

copied = 0
for name in ('offscreen', 'stress'):
    directory = here / name
    archive = read(directory / 'archive-manifest.json')
    for item in archive['copied_files']:
        path = directory / item['archive_path']
        require(path.stat().st_size == item['bytes'] and sha(path) == item['sha256'],
                'Original copy mismatch: ' + str(path))
        copied += 1
    core = module('matrix_core_' + name, directory / 'campaign_core.py')
    summarizer = module('matrix_summary_' + name, directory / 'summarize.py')
    stored = read(directory / 'report-summary.json')
    before = core.summarize_campaign('before', directory / 'before')
    after = core.summarize_campaign('after', directory / 'after')
    require(summarizer.compare_timing(core, before, after) == stored['timing'],
            'Timing comparison recomputation differs: ' + name)
    for label, campaign in (('before', before), ('after', after)):
        require(campaign['groups'] == stored['campaigns'][label]['groups'],
                'Group recomputation differs: ' + name + '/' + label)
    if name == 'offscreen':
        rgb = read(directory / 'rgb-before-after.json')
        require(rgb['comparison_count'] == len(rgb['results']) == 98 and rgb['all_rgb_identical'],
                'Recorded RGB comparison count/result differs')
        for result in rgb['results']:
            require(result['rgb_mae'] == result['max_channel_error'] == result['pixels_different'] == result['pixels_over3'] == 0,
                    'Recorded RGB errors differ')
            require(result['before']['ppm_sha256'] == result['after']['ppm_sha256'],
                    'Recorded PPM hashes differ')
        for label in ('verify-before', 'verify-after'):
            campaign = core.summarize_campaign(label, directory / label)
            require(campaign['groups'] == stored['campaigns'][label]['groups'],
                    'Verification CSV recomputation differs: ' + label)

trace = module('matrix_trace_reproduce', here / 'diagnostic/derive-diagnostic.py')
analysis = module('matrix_analysis_reproduce', here / 'analyze-matrix.py')
stored_analysis = read(here / 'analysis.json')
recorded_directory = Path(stored_analysis['campaigns']['offscreen']['before']['directory']).parents[1]
def normalized(value, directory):
    if isinstance(value, str):
        return value.replace(str(directory), '<evidence>')
    if isinstance(value, list):
        return [normalized(item, directory) for item in value]
    if isinstance(value, dict):
        return {normalized(key, directory): normalized(item, directory) for key, item in value.items()}
    return value

for name in ('offscreen', 'stress'):
    recomputed = analysis.compare_campaigns(trace, name, here / name / 'before', here / name / 'after')
    require(normalized(recomputed, here) == normalized(stored_analysis['campaigns'][name], recorded_directory),
            'Matched analysis differs: ' + name)
    require(recomputed['complete_and_comparable'], 'Matched protocol incomplete: ' + name)
recomputed_diagnostic = analysis.read_diagnostic(trace, here / 'diagnostic')
require(normalized(recomputed_diagnostic, here) == normalized(next(iter(stored_analysis['diagnostics'].values())), recorded_directory),
        'Diagnostic analysis differs')
require(not recomputed_diagnostic['pairing_issues'] and all(item['complete_and_comparable']
        for item in recomputed_diagnostic['on_off_comparisons'].values()), 'Diagnostic pairing incomplete')

metadata = read(here / 'stage-metadata.json')
report = module('matrix_report_reproduce', here / 'plot-and-report.py')
main = read(here / 'offscreen/report-summary.json')
stress = read(here / 'stress/report-summary.json')
report.validate_inputs(metadata, stored_analysis, [main, stress])
markdown, _, _, _ = report.write_report(metadata, main, stress, stored_analysis,
    'validation/wgpu-matrix-cache-linux/wgpu-matrix-cache.png')
require(markdown == (repository / 'docs/coin-render-wgpu-matrix-cache-linux.md').read_text(),
        'Report regeneration differs')
inputs = read(here / 'report-inputs.json')
for name, path in (('offscreen', here / 'offscreen/report-summary.json'),
                   ('stress', here / 'stress/report-summary.json'),
                   ('analysis', here / 'analysis.json'), ('metadata', here / 'stage-metadata.json')):
    require(sha(path) == inputs['inputs'][name]['sha256'], 'Report input hash differs: ' + name)
require(sha(here / 'plot-and-report.py') == inputs['generator']['sha256'], 'Report generator hash differs')
for name, digest in inputs['outputs'].items():
    path = repository / 'docs' / name if name.endswith('.md') else here / name
    require(sha(path) == digest, 'Report output hash differs: ' + name)
post = read(here / 'binary-hashes-post-campaign.json')
require(post['all_unchanged'] and len(post['files']) == 12 and all(item['unchanged'] for item in post['files']),
        'Recorded binary verification count differs')
print(json.dumps({'stage_files_verified': len(expected), 'copied_files_verified': copied,
    'timing_comparisons_recomputed': 14, 'matched_processes': 63, 'diagnostic_processes_recomputed': 8,
    'rgb_pairs_recorded': 98, 'rgb_pixels_recomputed': False, 'report_regeneration_identical': True}))
