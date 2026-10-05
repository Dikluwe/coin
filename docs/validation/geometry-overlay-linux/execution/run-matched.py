#!/usr/bin/env python3
"""Run interleaved focused steady geometry-overlay campaigns after gates and source commit.

--dry-run prints the plan without Git, builds, benchmark or GPU execution.
The control is one CoinGL process copied byte-for-byte to both result trees.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path('/tmp/coin-render-first-frame')
BEFORE_BGFX = '9a594fa39c7ca7924f9931863d4bdd7a37165cee'
BEFORE_WGPU = BEFORE_BGFX
CONTROL = '4d63bb993022ee8d40802558b0871a4803002b8d'
VARIANTS = 'coingl,bgfx-vulkan,bgfx-opengl,wgpu-vulkan'
PROFILES = (
    ('steady', 'geometry-10,geometry-100,transforms-10,materials-10', 3, 5, 15),
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def campaign_plan(revision, snapshot, prefix=Path('/tmp/coin-render-geometry-overlay')):
    common = [sys.executable, '/tmp/coin-render-geometry-overlay-matched.py',
              '--before-bgfx-build', '/tmp/coin-render-geometry-overlay-baseline/bgfx',
              '--before-wgpu-build', '/tmp/coin-render-geometry-overlay-baseline/wgpu',
              '--after-bgfx-build', '/tmp/coin-render-first-frame-bgfx',
              '--after-wgpu-build', '/tmp/coin-render-first-frame-wgpu',
              '--coingl-build', '/tmp/coin-render-material-geometry-baseline/coingl',
              '--scene', '/tmp/coin-render-city-40000.iv', '--scope', 'offscreen',
              '--gpu', 'nvidia', '--size', '1024', '--variants', VARIANTS,
              '--before-source-content-revision', BEFORE_BGFX,
              '--before-bgfx-source-content-revision', BEFORE_BGFX,
              '--before-wgpu-source-content-revision', BEFORE_WGPU,
              '--after-source-content-revision', revision,
              '--source-snapshot-revision', snapshot,
              '--control-source-content-revision', CONTROL,
              '--runner', str(ROOT/'scripts/coinrender/run_animation_benchmark.py')]
    roles = 1 + 2 * (len(VARIANTS.split(',')) - 1)
    result = []
    for name, cases, rounds, warmup, frames in PROFILES:
        processes = len(cases.split(',')) * rounds * roles
        command = common + ['--rounds', str(rounds), '--warmup', str(warmup),
                            '--cases', cases, '--frames', str(frames),
                            '--output-before', str(prefix)+'-'+name+'-before',
                            '--output-after', str(prefix)+'-'+name+'-after']
        result.append({'name': name, 'command': command,
                       'source_content_revision': revision,
                       'variant_source_content_revisions_before': {
                           'bgfx-vulkan': BEFORE_BGFX, 'bgfx-opengl': BEFORE_BGFX,
                           'wgpu-vulkan': BEFORE_WGPU, 'coingl': CONTROL},
                       'expected_unique_counts': {'processes': processes,
                                                  'measured_frames': processes*frames,
                                                  'warmup_frames': processes*warmup}})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-content-revision', required=True, help='Actual committed content built into the after libraries')
    parser.add_argument('--source-snapshot-revision', required=True, help='Actual checkout snapshot supplied by the owner')
    parser.add_argument('--output-prefix', type=Path, default=Path('/tmp/coin-render-geometry-overlay'))
    parser.add_argument('--commands-output', type=Path, default=Path('/tmp/coin-render-geometry-overlay-campaign-commands.json'))
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    revision = args.source_content_revision
    import re
    if any(re.fullmatch('[0-9a-f]{40}',value) is None for value in (revision,args.source_snapshot_revision)):
        parser.error('Source content and snapshot revisions must be complete SHAs')
    campaigns = campaign_plan(revision, args.source_snapshot_revision, args.output_prefix)
    metadata = {'schema_version': 1, 'source_content_revision': revision,
                'invocation': sys.argv, 'script': str(Path(__file__).resolve()),
                'script_sha256': sha256(Path(__file__)),
                'matched_script_sha256': sha256(Path('/tmp/coin-render-geometry-overlay-matched.py')),
                'expected_unique_counts': {
                    key: sum(item['expected_unique_counts'][key] for item in campaigns)
                    for key in ('processes', 'measured_frames', 'warmup_frames')},
                'commands': campaigns}
    if args.dry_run:
        print(json.dumps(metadata, indent=2))
        return
    if args.commands_output.exists():
        parser.error('Use a fresh commands-output file to preserve prior execution metadata')
    for item in campaigns:
        for option in ('--output-before', '--output-after'):
            output = Path(item['command'][item['command'].index(option)+1])
            if output.exists():
                parser.error('Use fresh campaign outputs: '+str(output))
    def save():
        args.commands_output.parent.mkdir(parents=True, exist_ok=True)
        args.commands_output.write_text(json.dumps(metadata, indent=2)+'\n')
    save()
    for item in campaigns:
        logfile = Path(str(args.output_prefix)+'-'+item['name']+'-run.log')
        item['orchestration_log'] = str(logfile)
        save()
        print('CAMPAIGN', item['name'], flush=True)
        with logfile.open('w') as stream:
            result = subprocess.run(item['command'], cwd=ROOT, stdout=stream,
                                    stderr=subprocess.STDOUT, text=True)
        item['exit_code'] = result.returncode
        save()
        if result.returncode:
            raise SystemExit(result.returncode)
        print('FINISHED', item['name'], flush=True)


if __name__ == '__main__':
    main()
