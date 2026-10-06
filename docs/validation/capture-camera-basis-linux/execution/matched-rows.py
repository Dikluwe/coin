#!/usr/bin/env python3
"""Measure matched wgpu before/after processes with one shared CoinGL control.

This temporary orchestration tool imports the repository animation runner's
case, environment, hashing, CSV statistics and JSON functions. Images are never
captured in this timing campaign. A CoinGL process runs once per case/round and
its exact CSV/log/result is preserved in both exclusive output directories.
"""

import argparse
import copy
import importlib.util
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess


DEFAULT_RUNNER = Path('/tmp/coin-render-first-frame/scripts/coinrender/run_animation_benchmark.py')
ROLES = ('coingl', 'wgpu-before', 'wgpu-after')
VARIANTS = ('coingl', 'wgpu-vulkan')


def load_runner(path):
    spec = importlib.util.spec_from_file_location('coin_animation_runner', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def row_from_log(runner, log, sample_path, *, case, variant, round_number, args):
    stats = runner.csv_stats(sample_path, args.frames)
    stem = f'{case}-{variant}-{round_number}'
    row = {'case': case, 'variant': variant, 'round': round_number,
           'scope': args.scope, 'gpu': args.gpu, 'stats': stats,
           'peak_rss_kib': int(re.search(r'benchmark_peak_rss_kib=(\d+)', log)[1]),
           'log': str(Path('logs') / f'{stem}.log'),
           'samples': str(Path('samples') / f'{stem}.csv')}
    first = re.search(r'^\S+_first_frame_ms=([\d.eE+-]+)', log, re.M)
    if first:
        row['first_ms'] = float(first[1])
    detail = re.search(r'^(?:\S+_first_detail|window_first_frame_detail) (.+)$', log, re.M)
    if detail:
        fields = dict(re.findall(r'(\w+)=([\d.eE+-]+)(?:\s|$)', detail[1]))
        if args.scope == 'window':
            row['first_ms'] = float(fields['render_present_ms'])
        total_key = 'total_ms' if args.scope == 'window' else 'total_with_update_ms'
        row['first_total_ms'] = float(fields[total_key])
        row['result_since_main_ms'] = float(fields['result_since_main_ms'])
    checksum = re.search(r'^(?:gl_)?rgba_fnv64=(\S+)', log, re.M)
    if checksum:
        row['final_rgba_checksum'] = checksum[1]
    throughput = re.search(r'^\S+_throughput frames=\d+ total_ms=([\d.eE+-]+) fps=([\d.eE+-]+)', log, re.M)
    if throughput:
        row['wall_total_ms'], row['throughput_fps'] = map(float, throughput.groups())
    elif args.scope == 'window':
        report = next(line for line in log.splitlines() if line.startswith('window_benchmark '))
        fields = dict(re.findall(r'(\w+)=([^\s]+)', report))
        row['wall_total_ms'] = float(fields['total_ms'])
        row['throughput_fps'] = float(fields['throughput_fps'])
        row['throughput_scope'] = fields['throughput_scope']
        row['final_gpu_drain_ms'] = float(fields['final_gpu_drain_ms'])
    row['animation_metadata'] = [line for line in log.splitlines()
                                 if line.startswith('animation') or 'selection_digest=' in line]
    if variant == 'coingl':
        row['shared_control'] = True
    return row


def write_medians(runner, out, rows, cases, args):
    summary = []
    for case in cases:
        for variant in VARIANTS:
            group = [row for row in rows if row['case'] == case and row['variant'] == variant]
            assert len(group) == args.rounds
            item = {'case': case, 'variant': variant, 'processes': len(group),
                    'scope': args.scope, 'gpu': args.gpu, 'stats': {}}
            for metric in ('update_ms', 'render_ms', 'publication_ms', 'total_ms'):
                item['stats'][metric] = {key: statistics.median(row['stats'][metric][key] for row in group)
                                         for key in group[0]['stats'][metric]}
            item['peak_rss_kib'] = statistics.median(row['peak_rss_kib'] for row in group)
            for key in ('first_ms', 'first_total_ms', 'result_since_main_ms', 'wall_total_ms',
                        'throughput_fps', 'final_gpu_drain_ms'):
                if all(key in row for row in group):
                    item[key] = statistics.median(row[key] for row in group)
            if all('throughput_scope' in row for row in group):
                assert len({row['throughput_scope'] for row in group}) == 1
                item['throughput_scope'] = group[0]['throughput_scope']
            if variant == 'coingl':
                item['shared_control'] = True
            summary.append(item)
    runner.write_json(out / 'medians.json', summary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-wgpu-build', type=Path, required=True)
    parser.add_argument('--after-wgpu-build', type=Path, required=True)
    parser.add_argument('--bgfx-build', type=Path, required=True, help='CoinGL control binaries/libraries')
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--output-before', type=Path, required=True)
    parser.add_argument('--output-after', type=Path, required=True)
    parser.add_argument('--cases', default='transforms-10')
    parser.add_argument('--scope', choices=('offscreen', 'window'), default='offscreen')
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--warmup', type=int, default=60)
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--gpu', choices=('nvidia', 'amd'), default='nvidia')
    parser.add_argument('--size', type=int, default=1024)
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--runner', type=Path, default=DEFAULT_RUNNER)
    parser.add_argument('--before-source-content-revision', '--before-source-revision', default=None,
                        help='Actual source content revision of the frozen before binaries')
    parser.add_argument('--after-source-content-revision', '--after-source-revision', default=None,
                        help='Actual source content revision of the after binaries')
    parser.add_argument('--control-source-content-revision', '--control-source-revision', default=None,
                        help='Actual source content revision of the CoinGL control binaries')
    args = parser.parse_args()
    if not args.runner.is_file():
        parser.error('Missing repository animation runner')
    runner = load_runner(args.runner.resolve())
    cases = [case.strip() for case in args.cases.split(',')]
    if not cases or any(not case for case in cases) or len(cases) != len(set(cases)):
        parser.error('Cases must be nonempty and unique')
    try:
        for case in cases:
            runner.case_options(case)
    except ValueError as error:
        parser.error(str(error))
    if not args.scene.is_file() or args.frames < 1 or args.warmup < 0 or args.rounds < 1 or args.size < 1:
        parser.error('Missing scene or invalid sample counts/size')
    outputs = {'wgpu-before': args.output_before.resolve(), 'wgpu-after': args.output_after.resolve()}
    before, after = outputs['wgpu-before'], outputs['wgpu-after']
    if before == after or before in after.parents or after in before.parents:
        parser.error('Before/after outputs must be distinct, non-nested directories')
    if any(out.exists() for out in outputs.values()):
        parser.error('Output directories must not already exist; use exclusive fresh directories')
    builds = {'coingl': args.bgfx_build.resolve(),
              'wgpu-before': args.before_wgpu_build.resolve(),
              'wgpu-after': args.after_wgpu_build.resolve()}
    binary_name = 'coin_render_gl_benchmark' if args.scope == 'offscreen' else 'coin_render_window_benchmark'
    hashes = {}
    for role, build in builds.items():
        hashes[role] = {}
        for path in (build / 'bin' / binary_name, build / 'lib/libCoinRender.so', build / 'lib/libCoin.so.80'):
            if not path.is_file():
                parser.error(f'Missing binary/library for {role}: {path}')
            hashes[role][str(path)] = runner.sha256(path)
    revisions = {'wgpu-before': args.before_source_content_revision,
                 'wgpu-after': args.after_source_content_revision,
                 'coingl': args.control_source_content_revision}
    root = args.runner.resolve().parents[2]
    revision = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, capture_output=True, text=True)
    runner_revision = revision.stdout.strip() if revision.returncode == 0 else None
    manifests, rows = {}, {}
    parameters = {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}
    for role, out in outputs.items():
        out.mkdir(parents=True, exist_ok=False)
        for subdir in ('logs', 'samples', 'images'):
            (out / subdir).mkdir()
        manifests[role] = {'parameters': dict(parameters, mode='measure', variants=','.join(VARIANTS),
                                               wgpu_build=str(builds[role]), output=str(out)),
                           'scene_sha256': runner.sha256(args.scene),
                           'binary_hashes': dict(hashes['coingl'], **hashes[role]),
                           'commands': [], 'execution_order': [],
                           'source_revision': revisions[role],
                           'source_content_revision': revisions[role],
                           'variant_source_content_revisions': {'coingl': revisions['coingl'],
                                                                 'wgpu-vulkan': revisions[role]},
                           'runner_source_revision': runner_revision,
                           'runner_path': str(args.runner.resolve()),
                           'runner_sha256': runner.sha256(args.runner),
                           'matched_runner_sha256': runner.sha256(Path(__file__)),
                           'matched_role': role, 'shared_control': True,
                           'shared_control_output': str(before),
                           'matched_peer_output': str(after if role == 'wgpu-before' else before)}
        rows[role] = []
        runner.write_json(out / 'manifest.json', manifests[role])
        runner.write_json(out / 'results.json', rows[role])
    execution_index = 0
    for round_index in range(args.rounds):
        case_order = cases[round_index % len(cases):] + cases[:round_index % len(cases)]
        if round_index % 2:
            case_order.reverse()
        for case_index, case in enumerate(case_order):
            offset = (round_index + case_index) % len(ROLES)
            role_order = list(ROLES[offset:] + ROLES[:offset])
            if round_index % 2:
                role_order.reverse()
            for role in role_order:
                execution_index += 1
                variant = 'coingl' if role == 'coingl' else 'wgpu-vulkan'
                destinations = list(outputs) if role == 'coingl' else [role]
                executing_role = 'wgpu-before' if role == 'coingl' else role
                out, build = outputs[executing_role], builds[role]
                animation, percent = runner.case_options(case)
                stem = f'{case}-{variant}-{round_index + 1}'
                sample_path = out / 'samples' / f'{stem}.csv'
                log_path = out / 'logs' / f'{stem}.log'
                command = [str(build / 'bin' / binary_name), '--backend',
                           ('gl' if variant == 'coingl' else 'wgpu') if args.scope == 'offscreen'
                           else ('coin-gl' if variant == 'coingl' else 'wgpu-vulkan'),
                           '--scene', str(args.scene.resolve()), '--animation', animation,
                           '--animated-percent', str(percent), '--transparency', 'object',
                           '--warmup', str(args.warmup), '--frames', str(args.frames),
                           '--samples-output', str(sample_path)]
                command += ['--size', str(args.size)] if args.scope == 'offscreen' else [
                    '--width', str(args.size), '--height', str(args.size)]
                env = runner.environment(build, variant, args.gpu)
                saved_env = {key: env[key] for key in ('LD_LIBRARY_PATH', 'VK_ICD_FILENAMES',
                    '__NV_PRIME_RENDER_OFFLOAD', '__GLX_VENDOR_LIBRARY_NAME', 'COIN_BGFX_RENDERER',
                    'WGPU_BACKEND', 'COIN_GLX_PIXMAP_DIRECT_RENDERING', 'COIN_GLXGLUE_NO_PBUFFERS') if key in env}
                command_record = {'stem': stem, 'command': command, 'environment': saved_env,
                                  'matched_role': role, 'execution_index': execution_index,
                                  'source_content_revision': revisions[role],
                                  'shared_control': role == 'coingl'}
                if role == 'coingl':
                    command_record['executed_in_output'] = str(before)
                    command_record['copied_to_output'] = str(after)
                order_record = {'case': case, 'round': round_index + 1, 'matched_role': role,
                                'variant': variant, 'stem': stem, 'execution_index': execution_index}
                for output_role in outputs:
                    manifests[output_role]['execution_order'].append(order_record)
                    if output_role in destinations:
                        manifests[output_role]['commands'].append(copy.deepcopy(command_record))
                    runner.write_json(outputs[output_role] / 'manifest.json', manifests[output_role])
                print(f'START {args.scope}/{stem} matched_role={role} execution_index={execution_index}', flush=True)
                try:
                    process = subprocess.run(['/usr/bin/time', '-f', 'benchmark_peak_rss_kib=%M', *command],
                                             env=env, capture_output=True, text=True, timeout=args.timeout)
                except subprocess.TimeoutExpired as error:
                    stdout, stderr = error.stdout or '', error.stderr or ''
                    if isinstance(stdout, bytes):
                        stdout = stdout.decode('utf-8', errors='replace')
                    if isinstance(stderr, bytes):
                        stderr = stderr.decode('utf-8', errors='replace')
                    log_path.write_text(stdout + stderr + '\nmatched_campaign_timeout=True\n', encoding='utf-8')
                    raise
                log = process.stdout + process.stderr
                log_path.write_text(log, encoding='utf-8')
                if process.returncode:
                    raise RuntimeError(f'{stem}/{role}: return={process.returncode}; {log[-2000:]}')
                if variant != 'coingl' and args.gpu == 'nvidia':
                    assert 'NVIDIA' in log, f'Unexpected adapter for {stem}/{role}'
                row = row_from_log(runner, log, sample_path, case=case, variant=variant,
                                   round_number=round_index + 1, args=args)
                if role == 'coingl':
                    shutil.copy2(log_path, after / 'logs' / log_path.name)
                    shutil.copy2(sample_path, after / 'samples' / sample_path.name)
                    assert runner.sha256(sample_path) == runner.sha256(after / 'samples' / sample_path.name)
                    assert runner.sha256(log_path) == runner.sha256(after / 'logs' / log_path.name)
                for output_role in destinations:
                    rows[output_role].append(copy.deepcopy(row))
                    runner.write_json(outputs[output_role] / 'results.json', rows[output_role])
                total = row['stats']['total_ms']
                print(f"DONE {stem}/{role}: total median={total['median_ms']:.3f} p95={total['p95_ms']:.3f} "
                      f"p99={total['p99_ms']:.3f} update={row['stats']['update_ms']['median_ms']:.3f} ms", flush=True)
    for role, out in outputs.items():
        write_medians(runner, out, rows[role], cases, args)
        assert not list((out / 'images').iterdir()), 'Timing campaigns must not capture images'
    print(f'Completed {execution_index} interleaved processes; each output contains '
          f'{len(cases) * args.rounds * 2} results, including identical shared controls', flush=True)


if __name__ == '__main__':
    main()
