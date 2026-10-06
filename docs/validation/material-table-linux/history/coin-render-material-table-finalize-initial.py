#!/usr/bin/env python3
"""Collect completed evidence, produce the report, and inventory the archive."""
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('/tmp/coin-render-first-frame')
BASE = Path('/tmp')
PREFIX = 'coin-render-material-table'
STAGE = ROOT / 'docs/validation/material-table-linux'
GENERATED = BASE / (PREFIX + '-generated-report')
REPORT = 'coin-render-material-table-linux.md'
assert not STAGE.exists() and not GENERATED.exists(), 'Use a fresh evidence destination.'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

records = []
def run(label, argv, script):
    log = BASE / (PREFIX + '-' + label + '.log')
    assert not log.exists(), 'Preserve actual execution log: ' + str(log)
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    with log.open('w') as output:
        completed = subprocess.run(argv, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
    records.append({'label': label, 'command': argv, 'exit_code': completed.returncode,
                    'script_path': str(script), 'script_sha256': digest(script),
                    'started_at_utc': started, 'ended_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    'log_path': str(log), 'log_sha256': digest(log)})
    print(label, 'exit', completed.returncode, flush=True)
    if completed.returncode:
        print(log.read_text(), flush=True)
        failure = BASE / (PREFIX + '-archive-failed-command.json')
        failure.write_text(json.dumps({'commands': records}, indent=2) + '\n')
        raise SystemExit(completed.returncode)
    return log

collector = BASE / (PREFIX + '-collect.py')
collect_log = run('archive-collect', ['python3', '-B', str(collector), '--root', str(ROOT)], collector)
reporter = STAGE / 'plot-and-report.py'
argv = ['python3', '-B', str(reporter), '--metadata', str(STAGE / 'stage-metadata.json'),
        '--quiet', str(STAGE / 'quiet/results.json'),
        '--diagnostic-primary', str(STAGE / 'diagnostic-analysis.json'),
        '--diagnostic-dimensional', str(STAGE / 'diagnostic-analysis.json'),
        '--rgb', str(STAGE / 'rgb/before-after.json'), '--output', str(GENERATED)]
report_log = run('archive-report', argv, reporter)
shutil.copyfile(GENERATED / REPORT, ROOT / 'docs' / REPORT)
for name in ['material-table.png', 'material-table.svg', 'report-inputs.json']:
    shutil.copyfile(GENERATED / name, STAGE / name)
execution = STAGE / 'execution'
for log in [collect_log, report_log]:
    shutil.copyfile(log, execution / log.name)
shutil.copyfile(Path(__file__), execution / 'finalize-archive.py')
record = {'commands': records, 'orchestrator_sha256': digest(Path(__file__)),
          'scope': 'CPU evidence collection and report/figure generation after all runtime; no GPU/build command.'}
card = BASE / (PREFIX + '-archive-command.json')
card.write_text(json.dumps(record, indent=2, ensure_ascii=False, allow_nan=False) + '\n')
shutil.copyfile(card, execution / card.name)
files = [path for path in STAGE.rglob('*') if path.is_file() and path.name != 'stage-files.json']
files.append(ROOT / 'docs' / REPORT)
inventory = {'inventory_excludes_itself': True,
             'files': [{'path': str(path.relative_to(ROOT)), 'bytes': path.stat().st_size, 'sha256': digest(path)}
                       for path in sorted(files)]}
(STAGE / 'stage-files.json').write_text(json.dumps(inventory, indent=2, ensure_ascii=False, allow_nan=False) + '\n')
print('Archive complete:', len(files), 'inventoried files;', sum(item['bytes'] for item in inventory['files']), 'bytes', flush=True)
