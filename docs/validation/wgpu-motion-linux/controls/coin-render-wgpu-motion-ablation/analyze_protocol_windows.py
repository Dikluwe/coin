#!/usr/bin/env python3
"""Recompute protocol windows from existing CSVs; never runs a benchmark."""
from pathlib import Path
import csv,datetime,hashlib,json,math,statistics
ROOT=Path('/tmp/coin-render-wgpu-motion-ablation')
FINAL=Path('/tmp/coin-render-wgpu-motion-final-after')
METRICS=('update_ms','render_ms','publication_ms','total_ms')

def stats(values):
    ordered=sorted(values)
    def nearest_rank(percent): return ordered[max(0,math.ceil(len(ordered)*percent)-1)]
    return {'count':len(values),'median_ms':statistics.median(ordered),
            'mean_ms':statistics.mean(ordered),'p95_ms':nearest_rank(.95),
            'p99_ms':nearest_rank(.99),'min_ms':ordered[0],'max_ms':ordered[-1]}

def record(path,case,config,round_index,expected):
    raw=path.read_bytes()
    rows=list(csv.DictReader(raw.decode().splitlines()))
    measured=[row for row in rows if row['warmup']=='0']
    measured.sort(key=lambda row:int(row['logical_frame']))
    complete=len(measured)==expected and [int(row['logical_frame']) for row in measured]==list(range(expected))
    intervals=[('all_measured',0,expected-1),('matched_0_19',0,19)]
    if expected>=30: intervals.append(('tail_20_29',20,29))
    intervals.extend((f'block_{lo}_{lo+4}',lo,lo+4) for lo in range(0,expected,5))
    windows=[]
    for name,lo,hi in intervals:
        subset=[row for row in measured if lo<=int(row['logical_frame'])<=hi]
        if len(subset)!=hi-lo+1: continue
        windows.append({'name':name,'logical_frame_start':lo,'logical_frame_end':hi,
                        'metrics':{metric:stats([float(row[metric]) for row in subset]) for metric in METRICS}})
    return {'source_csv':str(path),'source_sha256':hashlib.sha256(raw).hexdigest(),
            'case':case,'configuration':config,'round':round_index,
            'expected_measured_frames':expected,'observed_measured_frames':len(measured),
            'observed_warmup_frames':len(rows)-len(measured),'complete':complete,
            'total_ms_by_logical_frame':[{'logical_frame':int(row['logical_frame']),
                                         'total_ms':float(row['total_ms'])} for row in measured],
            'windows':windows}

records=[]
for case in ('transforms-10','transforms-100'):
    for config in ('full','cpp-only','arena-only'):
        for rid in (1,2,3):
            path=ROOT/f'{case}-{config}-{rid}.csv'
            if path.exists(): records.append(record(path,case,'ablation-prototype-'+config,rid,20))
    for rid in (1,2,3):
        path=FINAL/'samples'/f'{case}-wgpu-vulkan-{rid}.csv'
        if path.exists(): records.append(record(path,case,'final-restored-rust',rid,30))

def aggregate(case,config,window):
    per_process=[]
    for rec in records:
        if rec['case']!=case or rec['configuration']!=config or not rec['complete']: continue
        item=next((item for item in rec['windows'] if item['name']==window),None)
        if item: per_process.append({'round':rec['round'],'median_ms':item['metrics']['total_ms']['median_ms']})
    return {'configuration':config,'window':window,'completed_processes':len(per_process),
            'process_medians':per_process,
            'median_of_process_medians_ms':statistics.median(item['median_ms'] for item in per_process) if per_process else None}

comparison=[]
for case in ('transforms-10','transforms-100'):
    comparison.append({'case':case,'summaries':[
        aggregate(case,'ablation-prototype-cpp-only','all_measured'),
        aggregate(case,'final-restored-rust','matched_0_19'),
        aggregate(case,'final-restored-rust','all_measured'),
        aggregate(case,'final-restored-rust','tail_20_29')]})

report={'generated_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'purpose':'Explain measurement-window mismatch; this is a CSV-only snapshot during the final campaign.',
        'selection_rule':'warmup == 0; windows selected by inclusive logical_frame bounds',
        'statistics':'Median averages middle values; percentiles use nearest rank.',
        'protocols':{'ablation':{'warmup_frames':5,'measured_frames':20},
                     'final':{'warmup_frames':5,'measured_frames':30}},
        'metric_scope':'total_ms = scene update + render/readback + publication; not physical display FPS.',
        'records':records,'cross_protocol_window_summaries':comparison,
        'interpretation':[
          'Both campaigns show increasing frame times across the measured logical-frame sequence.',
          'The final 30-frame median includes ten slower tail frames absent from the 20-frame ablation.',
          'Matched logical frames 0..19 are much closer than comparing the original 20-frame and 30-frame medians.',
          'The prototype with reuse disabled still changed old-buffer lifetime and cache publication timing, so it is not source-identical to restored Rust.',
          'Explicit COPY_DST is unlikely to explain Vulkan allocation differences: wgpu-core already adds HAL COPY_DST for mapped-at-creation buffers without MAP_WRITE.',
          'These cross-protocol observations do not establish the cause of the ramp or attribute any timing difference to buffer lifetime.',
          'The retained conclusion is the interleaved same-binary 20-frame experiment: Rust rewrite ON did not outperform OFF, with the C++ optimization enabled in both configurations.',
          'Final conclusions must use the identical interleaved 30-frame before/after protocol; this snapshot may precede completion of all rounds.'
        ],
        'minimal_future_test_if_needed':[
          'Use identical scene, warmup, measured-frame count and logical-frame interval in interleaved A/B processes.',
          'For an isolated lifetime experiment, keep restored buffer usages and cache matching; only retain the previous FrameGpuBuffers until queue.submit instead of dropping it before new allocations.',
          'Measure phase times and per-frame samples; do not reintroduce staging rewrite, Arc changes or extra geometry scans.',
          'Record memory and enough warmup to characterize whether the observed ramp stabilizes.'
        ]}
source_paths=list(Path('/home/dikluwe/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f').glob('wgpu-core-24.0.*/src/device/resource.rs'))
report['copy_dst_source_evidence']=[{'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
    'line_start':527,'line_end':535,'finding':'mapped_at_creation and not MAP_WRITE adds HAL COPY_DST independently of requested VERTEX/INDEX usage flags'} for path in source_paths]
output=ROOT/'protocol-window-analysis.json'
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'output':str(output),'records':len(records),'complete_records':sum(r['complete'] for r in records),'summaries':comparison},indent=2))
