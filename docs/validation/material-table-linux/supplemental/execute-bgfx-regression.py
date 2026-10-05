#!/usr/bin/env python3
"""Run focused geometry-10 diagnostics only when invoked by the campaign owner.

Three APIs × three rounds × two modes = 18 processes.
Geometry-10: five warmups and fifteen measured frames each.
Totals: 270 measured frames and 90 warmups. GPU timestamps are disabled
because their query drain changes the queue. --dry-run prints
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
import os
import signal
import threading
import time
from datetime import datetime, timezone

sys.dont_write_bytecode = True
OPTOUT = 'COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION'
VARIANTS = ('wgpu-vulkan', 'bgfx-vulkan', 'bgfx-opengl')
PROFILES = {'geometry-10': {'animation': 'geometry', 'percent': 10, 'warmup': 5, 'frames': 15}}
EXPECTED = {'processes': 18, 'measured_frames': 270, 'warmup_frames': 90}


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
                               '--animation', profile['animation'], '--animated-percent', str(profile['percent']),
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


def cpu_observation(pid=None):
    record={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'monotonic_seconds':time.monotonic(),'proc':{},'processes':{},'errors':{}}
    for name in ('stat','pressure/cpu'):
        try:record['proc'][name]=Path('/proc',name).read_text()
        except OSError as error:record['errors'][name]=str(error)
    def process(process_id):
        base=Path('/proc',str(process_id));data={}
        for name in ('stat','schedstat'):
            try:data[name]=(base/name).read_text()
            except OSError as error:data[name+'_error']=str(error)
        try:
            data['tasks']={str(task.name):(task/'stat').read_text() for task in (base/'task').iterdir()}
        except OSError as error:data['tasks_error']=str(error)
        try:data['children']=(base/'task'/str(process_id)/'children').read_text().split()
        except OSError:data['children']=[]
        record['processes'][str(process_id)]=data
        return data['children']
    if pid:
        children=process(pid)
        for child in children:process(int(child))
    return record


def observed_run(command,cwd,environment,timeout):
    before=cpu_observation();events=[];stop=threading.Event()
    process=subprocess.Popen(command,cwd=cwd,env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
    events.append(cpu_observation(process.pid))
    def sampler():
        while not stop.wait(1):events.append(cpu_observation(process.pid))
    thread=threading.Thread(target=sampler,daemon=True);thread.start()
    timed_out=False
    try:stdout,stderr=process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out=True;os.killpg(process.pid,signal.SIGKILL);stdout,stderr=process.communicate()
    finally:stop.set();thread.join()
    return stdout,stderr,process.returncode,timed_out,{'before':before,'during':events,'after':cpu_observation(),'interval_seconds':1}


def hardware_snapshot():
    record={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'commands':[],'cpu_governors':{},'errors':{}}
    for command in (['uname','-a'],['lscpu'],['nvidia-smi','--query-gpu=name,driver_version,temperature.gpu,pstate,clocks.gr,clocks.mem','--format=csv,noheader']):
        try:
            result=subprocess.run(command,capture_output=True,text=True,timeout=30)
            record['commands'].append({'command':command,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
        except (OSError,subprocess.TimeoutExpired) as error:record['commands'].append({'command':command,'error':str(error)})
    for path in sorted(Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')):
        try:record['cpu_governors'][str(path)]=path.read_text().strip()
        except OSError as error:record['errors'][str(path)]=str(error)
    record['scope']='Read-only before/after hardware snapshot; no governor/clocks changed'
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repository', type=Path, default=Path('/tmp/coin-render-first-frame'))
    parser.add_argument('--wgpu-build', type=Path, default=Path('/tmp/coin-render-material-table-baseline/wgpu'))
    parser.add_argument('--bgfx-build', type=Path, default=Path('/tmp/coin-render-material-table-baseline/bgfx'))
    parser.add_argument('--scene', type=Path, default=Path('/tmp/coin-render-city-40000.iv'))
    parser.add_argument('--output', type=Path, default=Path('/tmp/coin-render-bgfx-regression-diagnostic'))
    parser.add_argument('--source-content-revision', default='96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33', help='Actual compiled source revision')
    parser.add_argument('--source-card',type=Path,default=Path('/tmp/coin-render-material-table-baseline-binaries.json'))
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if re.fullmatch('[0-9a-f]{40}', args.source_content_revision) is None:
        parser.error('--source-content-revision must be a complete Git SHA')
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    for name in ('repository', 'wgpu_build', 'bgfx_build', 'scene', 'output', 'source_card'):
        setattr(args, name, getattr(args, name).resolve())
    commands = plan(args)
    if args.dry_run:
        print(json.dumps({'expected_unique_counts': EXPECTED, 'profiles': PROFILES, 'optout': OPTOUT,
                          'commands': commands}, indent=2))
        return
    card=json.loads(args.source_card.read_text())
    if card['source_content_revision']!=args.source_content_revision or len(card['files'])!=8:
        raise ValueError('Pinned source card must cover8 compiled artifacts')
    if any(file_hash(Path(item['path']))!=item['sha256'] for item in card['files']):
        raise ValueError('Pinned artifact hash differs before diagnosis')
    args.output.mkdir(parents=True, exist_ok=False)
    runner_path = args.repository/'scripts/coinrender/run_animation_benchmark.py'
    spec = importlib.util.spec_from_file_location('geometry_overlay_benchmark_runner', runner_path)
    runner = importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
    metadata = {'source_content_revision': args.source_content_revision, 'scene_sha256': file_hash(args.scene),
                'binary_hashes': {}, 'commands': [], 'expected_unique_counts': EXPECTED,
                'parameters': {'variants': list(VARIANTS), 'rounds': 3, 'profiles': PROFILES,
                               'optout': OPTOUT, 'gpu': 'nvidia', 'transparency': 'object', 'size': 1024,
                               'order': 'API rotation each round; alternate on/off order',
                               'gpu_timestamps':False,'readback_pipeline_depth':1,
                               'purpose':'diagnostic phases; trace overhead included; preserve every frame and outlier'},
                'invocation': sys.argv, 'script_sha256': file_hash(Path(__file__).resolve()),
                'runner_script_sha256': file_hash(runner_path),
                'source_card':card,'source_card_input':{'path':str(args.source_card),'sha256':file_hash(args.source_card)},
                'hardware_before':hardware_snapshot(),'cpu_sampler':{'interval_seconds':1,'proc_clock_ticks_per_second':os.sysconf('SC_CLK_TCK'),'scope':'global CPU/pressure plus this subprocess children/tasks only; observation not causal proof'}}
    for build in (args.wgpu_build, args.bgfx_build):
        for relative in ('bin/coin_render_gl_benchmark', 'bin/coin_render_window_benchmark', 'lib/libCoinRender.so', 'lib/libCoin.so.80.0.10'):
            path = build/relative
            metadata['binary_hashes'][str(path)] = file_hash(path)
    metadata_path = args.output/'commands.json'
    write_metadata(metadata_path, metadata)
    for item in commands:
        build = Path(item['build'])
        environment = runner.environment(build, item['variant'], 'nvidia')
        environment.pop(OPTOUT, None)
        environment.pop('COIN_WGPU_GPU_TIMESTAMPS', None)
        environment.pop('COIN_BGFX_READBACK_PIPELINE_DEPTH', None)
        environment.pop('COIN_WGPU_TRACE_PHASES',None)
        environment['COIN_RENDER_TRACE_PHASES'] = '1'
        if item['mode'] == 'off':
            environment[OPTOUT] = '1'
        item['environment'] = {key: value for key, value in environment.items()
                               if key.startswith(('COIN_', 'WGPU_', '__NV', '__GLX', 'VK_', 'LD_LIBRARY'))}
        item['timeout_seconds'] = args.timeout
        metadata['commands'].append(item)
        write_metadata(metadata_path, metadata)
        print('START', item['stem'], flush=True)
        item['timed_command']=['/usr/bin/time','-f','benchmark_peak_rss_kib=%M',*item['command']]
        write_metadata(metadata_path, metadata)
        try:
            stdout,stderr,exit_code,timed_out,observations=observed_run(item['timed_command'],args.repository,environment,args.timeout)
            log=stdout+stderr
            (args.output/(item['stem']+'.stdout')).write_text(stdout)
            (args.output/(item['stem']+'.stderr')).write_text(stderr)
            cpu_path=args.output/(item['stem']+'.cpu-observation.json')
            write_metadata(cpu_path,observations)
            item['cpu_observation_input']={'path':str(cpu_path),'sha256':file_hash(cpu_path)}
            item['exit_code']=exit_code
            item['timed_out']=timed_out
        except subprocess.TimeoutExpired as error:
            log = output_text(error.stdout)+output_text(error.stderr)
            item['exit_code'] = None
            item['timed_out'] = True
            (args.output/(item['stem']+'.log')).write_text(log)
            write_metadata(metadata_path, metadata)
            raise RuntimeError(item['stem']+' timed out; raw output and command metadata preserved') from error
        (args.output/(item['stem']+'.log')).write_text(log)
        write_metadata(metadata_path, metadata)
        if item['exit_code']!=0 or item['timed_out']:
            raise RuntimeError(item['stem']+' failed; exit and raw log preserved')
        item['adapter_verified_nvidia'] = 'NVIDIA' in log
        item['trace_gpu_timestamps_absent'] = 'COIN_WGPU_GPU_TIMESTAMPS' not in item['environment']
        item['phase_trace_seen'] = 'COIN_RENDER_PHASE ' in log
        write_metadata(metadata_path, metadata)
        if not item['adapter_verified_nvidia'] or not item['trace_gpu_timestamps_absent'] or not item['phase_trace_seen']:
            raise RuntimeError(item['stem']+' did not identify NVIDIA')
        item['measured_csv_stats'] = runner.csv_stats(args.output/(item['stem']+'.csv'), PROFILES[item['case']]['frames'])
        write_metadata(metadata_path, metadata)
        print('END', item['stem'], flush=True)
    metadata['hardware_after']=hardware_snapshot()
    metadata['binary_hashes_post_campaign']={path:file_hash(Path(path)) for path in metadata['binary_hashes']}
    metadata['binaries_unchanged']=metadata['binary_hashes_post_campaign']==metadata['binary_hashes']
    write_metadata(metadata_path, metadata)
    if not metadata['binaries_unchanged']:raise RuntimeError('Binaries changed during ablation')
    metadata['completed_unique_counts'] = {'processes': len(metadata['commands']),
                                          'measured_frames': sum(PROFILES[item['case']]['frames'] for item in metadata['commands']),
                                          'warmup_frames': sum(PROFILES[item['case']]['warmup'] for item in metadata['commands'])}
    assert metadata['completed_unique_counts'] == EXPECTED
    write_metadata(metadata_path, metadata)


if __name__ == '__main__':
    main()
