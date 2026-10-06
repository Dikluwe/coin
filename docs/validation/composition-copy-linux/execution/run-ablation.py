#!/usr/bin/env python3
"""Run composition-copy on/off ablation only when invoked by the campaign owner.

Three APIs × three rounds × two modes × two cases = 36 processes.
Static: zero warmups, one measured frame. Transforms-10: five warmups, one
measured frame. Totals: 36 measured frames and 90 warmups. --dry-run prints
argv/protocol without launching anything or writing an output directory.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
OPTOUT = 'COIN_RENDER_DISABLE_COMPOSITION_BORROW'
VARIANTS = ('wgpu-vulkan', 'bgfx-vulkan', 'bgfx-opengl')
PROFILES = {'static': {'animation': 'static', 'warmup': 0, 'frames': 1},
            'transforms-10': {'animation': 'transforms', 'warmup': 5, 'frames': 1}}
EXPECTED = {'processes': 36, 'measured_frames': 36, 'warmup_frames': 90}


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plan(args):
    commands = []
    for round_index in range(3):
        variants = list(VARIANTS[round_index:]) + list(VARIANTS[:round_index])
        cases = list(PROFILES) if round_index % 2 == 0 else list(reversed(PROFILES))
        for variant in variants:
            build = args.wgpu_build if variant.startswith('wgpu') else args.bgfx_build
            for case_index, case in enumerate(cases):
                profile = PROFILES[case]
                modes = ('on', 'off') if (round_index + case_index) % 2 == 0 else ('off', 'on')
                for mode in modes:
                    stem = f'{variant}-{case}-{mode}-{round_index+1}'
                    command = [str(build/'bin/coin_render_gl_benchmark'), '--backend',
                               'wgpu' if variant.startswith('wgpu') else 'bgfx', '--scene', str(args.scene),
                               '--animation', profile['animation'], '--animated-percent', '10',
                               '--transparency', 'object', '--size', '1024',
                               '--warmup', str(profile['warmup']), '--frames', str(profile['frames']),
                               '--samples-output', str(args.output/(stem+'.csv'))]
                    commands.append({'stem': stem, 'variant': variant, 'case': case, 'round': round_index+1,
                                     'mode': mode, 'build': str(build), 'command': command,
                                     'source_content_revision': args.source_content_revision})
    assert len(commands) == EXPECTED['processes']
    assert sum(PROFILES[item['case']]['frames'] for item in commands) == EXPECTED['measured_frames']
    assert sum(PROFILES[item['case']]['warmup'] for item in commands) == EXPECTED['warmup_frames']
    return commands


def output_text(value):
    return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else (value or '')


def write_metadata(path, metadata):
    path.write_text(json.dumps(metadata, indent=2, allow_nan=False)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repository', type=Path, default=Path('/tmp/coin-render-first-frame'))
    parser.add_argument('--wgpu-build', type=Path, default=Path('/tmp/coin-render-first-frame-wgpu'))
    parser.add_argument('--bgfx-build', type=Path, default=Path('/tmp/coin-render-first-frame-bgfx'))
    parser.add_argument('--scene', type=Path, default=Path('/tmp/coin-render-city-40000.iv'))
    parser.add_argument('--output', type=Path, default=Path('/tmp/coin-render-composition-copy-ablation'))
    parser.add_argument('--source-content-revision', required=True, help='Actual committed source built into both current render families')
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if re.fullmatch('[0-9a-f]{40}', args.source_content_revision) is None:
        parser.error('--source-content-revision must be a complete Git SHA')
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    for name in ('repository', 'wgpu_build', 'bgfx_build', 'scene', 'output'):
        setattr(args, name, getattr(args, name).resolve())
    commands = plan(args)
    if args.dry_run:
        print(json.dumps({'expected_unique_counts': EXPECTED, 'profiles': PROFILES, 'optout': OPTOUT,
                          'commands': commands}, indent=2))
        return
    args.output.mkdir(parents=True, exist_ok=False)
    runner_path = args.repository/'scripts/coinrender/run_animation_benchmark.py'
    spec = importlib.util.spec_from_file_location('composition_copy_benchmark_runner', runner_path)
    runner = importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
    metadata = {'source_content_revision': args.source_content_revision, 'scene_sha256': file_hash(args.scene),
                'binary_hashes': {}, 'commands': [], 'expected_unique_counts': EXPECTED,
                'parameters': {'variants': list(VARIANTS), 'rounds': 3, 'profiles': PROFILES,
                               'optout': OPTOUT, 'gpu': 'nvidia', 'transparency': 'object', 'size': 1024,
                               'order': 'API rotation each round; alternate case order and on/off order'},
                'invocation': sys.argv, 'script_sha256': file_hash(Path(__file__).resolve()),
                'runner_script_sha256': file_hash(runner_path)}
    for build in (args.wgpu_build, args.bgfx_build):
        for relative in ('bin/coin_render_gl_benchmark', 'lib/libCoinRender.so', 'lib/libCoin.so.80'):
            path = build/relative
            metadata['binary_hashes'][str(path)] = file_hash(path)
    metadata_path = args.output/'commands.json'
    write_metadata(metadata_path, metadata)
    for item in commands:
        build = Path(item['build'])
        environment = runner.environment(build, item['variant'], 'nvidia')
        environment.pop(OPTOUT, None)
        environment['COIN_RENDER_TRACE_PHASES'] = '1'
        if item['mode'] == 'off':
            environment[OPTOUT] = '1'
        item['environment'] = {key: value for key, value in environment.items()
                               if key.startswith(('COIN_', 'WGPU_', '__NV', '__GLX', 'VK_', 'LD_LIBRARY'))}
        item['timeout_seconds'] = args.timeout
        metadata['commands'].append(item)
        write_metadata(metadata_path, metadata)
        print('START', item['stem'], flush=True)
        try:
            result = subprocess.run(item['command'], cwd=args.repository, env=environment,
                                    capture_output=True, text=True, timeout=args.timeout)
            log = result.stdout+result.stderr
            item['exit_code'] = result.returncode
            item['timed_out'] = False
        except subprocess.TimeoutExpired as error:
            log = output_text(error.stdout)+output_text(error.stderr)
            item['exit_code'] = None
            item['timed_out'] = True
            (args.output/(item['stem']+'.log')).write_text(log)
            write_metadata(metadata_path, metadata)
            raise RuntimeError(item['stem']+' timed out; raw output and command metadata preserved') from error
        (args.output/(item['stem']+'.log')).write_text(log)
        write_metadata(metadata_path, metadata)
        if result.returncode:
            raise RuntimeError(item['stem']+' failed; exit and raw log preserved')
        item['adapter_verified_nvidia'] = 'NVIDIA' in log
        write_metadata(metadata_path, metadata)
        if not item['adapter_verified_nvidia']:
            raise RuntimeError(item['stem']+' did not identify NVIDIA')
        item['measured_csv_stats'] = runner.csv_stats(args.output/(item['stem']+'.csv'), PROFILES[item['case']]['frames'])
        write_metadata(metadata_path, metadata)
        print('END', item['stem'], flush=True)
    metadata['completed_unique_counts'] = {'processes': len(metadata['commands']),
                                          'measured_frames': sum(PROFILES[item['case']]['frames'] for item in metadata['commands']),
                                          'warmup_frames': sum(PROFILES[item['case']]['warmup'] for item in metadata['commands'])}
    assert metadata['completed_unique_counts'] == EXPECTED
    write_metadata(metadata_path, metadata)


if __name__ == '__main__':
    main()
