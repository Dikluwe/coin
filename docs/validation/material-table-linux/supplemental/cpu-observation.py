#!/usr/bin/env python3
"""Describe coarse Linux CPU observations; no causal or per-frame attribution."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def pressure(text):
    return {line.split()[0]:{key:float(value) for key,value in re.findall(r'(\w+)=([\d.]+)',line)} for line in text.splitlines()}


def cpu_totals(text):
    values=[int(value) for value in text.splitlines()[0].split()[1:]]
    # guest counters already contribute to user/nice; sum only through steal.
    return sum(values[:8]),values[3]+values[4]


def describe(path):
    observations=json.loads(path.read_text());before=observations['before'];after=observations['after']
    elapsed=after['monotonic_seconds']-before['monotonic_seconds']
    total0,idle0=cpu_totals(before['proc']['stat']);total1,idle1=cpu_totals(after['proc']['stat'])
    p0=pressure(before['proc']['pressure/cpu']);p1=pressure(after['proc']['pressure/cpu'])
    result={'input':{'path':str(path.resolve()),'sha256':sha(path)},'wall_seconds':elapsed,
            'during_snapshots':len(observations['during']),
            'global_busy_cpu_percent':100*(1-(idle1-idle0)/(total1-total0)),
            'global_pressure_stall_percent':{key:100*(p1[key]['total']-p0[key]['total'])/(elapsed*1e6) for key in p0},
            'global_pressure_before':p0,'global_pressure_after':p1,'benchmark_main_schedstat_intervals':[]}
    by_pid={}
    for sample in observations['during']:
        for pid,process in sample['processes'].items():
            raw=process.get('stat','')
            if '(coin_render_gl_' not in raw:continue
            if not process.get('schedstat'):continue
            by_pid.setdefault(pid,[]).append((sample['monotonic_seconds'],[int(value) for value in process['schedstat'].split()]))
    for pid,samples in by_pid.items():
        for (t0,a),(t1,b) in zip(samples,samples[1:]):
            result['benchmark_main_schedstat_intervals'].append({'pid':pid,'wall_seconds':t1-t0,
                'cpu_running_seconds':(b[0]-a[0])/1e9,'runnable_wait_seconds':(b[1]-a[1])/1e9,
                'timeslices':b[2]-a[2]})
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('/tmp/coin-render-bgfx-regression-diagnostic'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():raise ValueError('Output must be absent')
    result={'runs':{path.stem.removesuffix('.cpu-observation'):describe(path) for path in sorted(args.input.glob('*.cpu-observation.json'))},
            'script_sha256':sha(Path(__file__)),'limitations':['Global pressure/busy ratios include startup, warmup and measured frames.',
                'One-second snapshots cannot attribute scheduling or CPU frequency to an individual frame.',
                'schedstat describes only observed benchmark main-thread intervals; GPU worker waits are not isolated.',
                'System load observations do not establish the cause of earlier uninstrumented campaign variation.']}
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    for name,run in result['runs'].items():
        print(name,'busy=',round(run['global_busy_cpu_percent'],2),'pressure=',{key:round(value,2) for key,value in run['global_pressure_stall_percent'].items()},
              'main_runnable_wait_s=',round(sum(item['runnable_wait_seconds'] for item in run['benchmark_main_schedstat_intervals']),5))


if __name__=='__main__':main()
