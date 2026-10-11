#!/usr/bin/env python3
"""Repeat the first-text FreeCAD border gate in fresh host processes."""

import json
import os
from pathlib import Path
import subprocess
import sys


if len(sys.argv) != 3:
    raise SystemExit('usage: runner.py FREECAD ARTIFACTS')

freecad = Path(sys.argv[1]).resolve()
artifacts = Path(sys.argv[2]).resolve()
artifacts.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[3]
runner = root / 'testsuite/qt-quarter/run.py'
policies = [1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1]
results = []

for index, policy in enumerate(policies, 1):
    name = f'{index:02d}-' + ('portable' if policy else 'native')
    target = artifacts / name
    env = dict(os.environ, COIN_TEST_COLD_BORDER_QUALIFY='1',
               COIN_TEST_PORTABLE_SAMPLING=str(policy))
    command = [sys.executable, str(runner), '--artifacts', str(target),
               '--harness', str(freecad), '--freecad', str(freecad),
               '--case', 'freecad-sampling-policy', '--backend', 'wgpu',
               '--renderer', 'vulkan', '--mode', 'object', '--scale', '1',
               '--require-hardware', '--timeout', '120']
    completed = subprocess.run(command, env=env, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (target / 'runner.log').write_text(completed.stdout)
    result_file = target / 'vulkan-object-opaque-1x-freecad-sampling-policy/result.json'
    result = json.loads(result_file.read_text()) if result_file.is_file() else {}
    row = {'trial': name, 'policy': policy, 'exit_code': completed.returncode,
           'status': result.get('status'), 'hardware_gpu': result.get('hardware_gpu'),
           'gpu_submitted': result.get('gpu_submitted'),
           'cold_start_qualified': result.get('cold_start_qualified'),
           'cold_baseline_no_forced_redraw': result.get('cold_baseline_no_forced_redraw'),
           'prebaseline_redraws': result.get('prebaseline_redraws'),
           'cold_baseline_frame_count': result.get('cold_baseline_frame_count'),
           'cold_text_visible_changes': result.get('cold_text_visible_changes'),
           'cold_text_mutation_changes': result.get('cold_text_mutation_changes'),
           'cold_border_after_hide_changes': result.get('cold_border_after_hide_changes'),
           'cold_border_repeat_changes': result.get('cold_border_repeat_changes'),
           'reason': result.get('reason')}
    results.append(row)
    (artifacts / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    print(name, row['status'], row['cold_border_after_hide_changes'], flush=True)

if not all(row['exit_code'] == 0 and row['status'] == 'PASS' and
           row['hardware_gpu'] and row['gpu_submitted'] and
           row['cold_start_qualified'] and
           row['cold_border_after_hide_changes'] == 0 and
           row['cold_border_repeat_changes'] == 0 for row in results):
    raise SystemExit(1)
