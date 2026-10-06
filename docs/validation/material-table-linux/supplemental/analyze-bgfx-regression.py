#!/usr/bin/env python3
"""Read focused geometry diagnostic CSV/logs; never launch a benchmark.

Frames end at synchronous Action markers. Backend/Target phases precede that
mark and describe the same apply. CSV warmup flags choose measured rows; all
rows and raw events remain in the result. Notify is not separately timed.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import statistics
import sys
sys.dont_write_bytecode=True
OPTOUT='COIN_RENDER_DISABLE_GEOMETRY_INTERVAL_VALIDATION'
SCOPES=('action','target','bgfx','bgfx_geometry','validation_detail','composition_detail',
        'geometry_overlay_validation','bridge','rust','rust_cpu_detail','rust_instances','wgpu_opaque_instancing')


def require(ok,message):
    if not ok:raise ValueError(message)


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):return json.loads(path.read_text(),parse_constant=lambda x:(_ for _ in ()).throw(ValueError(x)))


def load(path):
    spec=importlib.util.spec_from_file_location('focused_trace_parser',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


def stats(values):
    ordered=sorted(values);return {'median':statistics.median(ordered),'minimum':ordered[0],
        'maximum':ordered[-1],'p95':ordered[math.ceil(.95*len(ordered))-1],
        'p99':ordered[math.ceil(.99*len(ordered))-1],'values':values}


def diff(off,on):return {'off':off,'on':on,'delta':on-off,'change_percent':(on/off-1)*100 if off else None}


def analyze(directory,trace_helper):
    helper=load(trace_helper);manifest=read(directory/'commands.json');runs={};issues=[]
    for command in manifest['commands']:
        stem=command['stem'];csv_path=directory/(stem+'.csv');log_path=directory/(stem+'.log')
        require(command.get('exit_code')==0 and not command.get('timed_out') and command.get('adapter_verified_nvidia') is True,
                'Command failed/adapter missing: '+stem)
        env=command['environment'];require(env.get('COIN_RENDER_TRACE_PHASES')=='1' and 'COIN_WGPU_GPU_TIMESTAMPS' not in env,
                                         'Trace/timestamp environment differs: '+stem)
        with csv_path.open(newline='') as stream:csv_rows=list(csv.DictReader(stream))
        require(csv_rows,'CSV empty: '+stem)
        measured=[]
        for index,row in enumerate(csv_rows):
            require(row['warmup'].lower() in ('0','1','true','false'),'Unknown warmup flag')
            if row['warmup'].lower() in ('0','false'):measured.append(index)
        profile=manifest['parameters']['profiles'][command['case']]
        require(len(csv_rows)==profile['warmup']+profile['frames'] and
                measured==list(range(profile['warmup'],len(csv_rows))) and
                [row['frame_index'] for row in csv_rows]==[str(index) for index in range(-profile['warmup'],profile['frames'])],
                'CSV protocol/logical indices differ: '+stem)
        warnings=[];events,series,scopes=helper.parse_trace(log_path.read_bytes(),warnings)
        actions=scopes.get('action',[]);require(len(actions)==len(csv_rows),'Action/CSV cardinality mismatch: '+stem)
        frames=[];previous=0
        for index,(csv_row,action) in enumerate(zip(csv_rows,actions)):
            segment=[event for event in events if previous < event['log_line_number'] <= action['log_line_number']]
            grouped={scope:[event for event in segment if event['scope']==scope] for scope in SCOPES}
            require(len(grouped['target'])==1 and len(grouped['action'])==1,'Target/action pairing differs: '+stem)
            expected_backend=('bridge','rust') if command['variant'].startswith('wgpu') else ('bgfx','bgfx_geometry')
            require(all(len(grouped[scope])==1 for scope in expected_backend),'Backend/action pairing differs: '+stem)
            require(action['fields'].get('plan_reuse')==('full_rebuild' if index==0 else 'resource_rebuild'),
                    'Unexpected reuse route: '+stem)
            require(len(grouped['geometry_overlay_validation'])==(0 if index==0 else 1), 'Geometry phase/action pairing differs: '+stem)
            metrics={key:float(value) for key,value in csv_row.items() if key.endswith('_ms')}
            require(all(math.isfinite(value) and value>=0 for value in metrics.values()),'Invalid CSV metric')
            for scope,records in grouped.items():
                if len(records)==1:
                    for key,value in records[0]['fields'].items():
                        if key.endswith('_ms') and helper.is_number(value):metrics[scope+'.'+key]=value
            action_fields=action['fields'];action_sum=sum(action_fields[key] for key in ('traversal_ms','frame_plan_ms','backend_ms'))
            metrics['derived.render_outside_action_phases_ms']=metrics['render_ms']-action_sum
            target=grouped['target'][0]['fields']
            metrics['derived.action_backend_outside_target_phases_ms']=action_fields['backend_ms']-sum(target[key] for key in ('validation_ms','prepare_ms','submit_ms'))
            if grouped['bgfx']:
                bgfx=grouped['bgfx'][0]['fields']
                additive=('lower_ms','upload_ms','encode_ms','read_request_ms','submit_frame_ms','read_wait_ms','gpu_query_drain_ms','row_flip_ms')
                metrics['derived.target_submit_outside_bgfx_phases_ms']=target['submit_ms']-sum(bgfx[key] for key in additive)
                require(bgfx['gpu_query_frames']==0 and bgfx['gpu_query_drain_ms']<1,'GPU timestamp drain unexpectedly active')
            frames.append({'csv_row_index':index,'warmup':index not in measured,'csv':csv_row,'metrics':metrics,
                           'action_log_line':action['log_line_number'],'events':segment,'scope_counts':{key:len(value) for key,value in grouped.items()}})
            previous=action['log_line_number']
        keys=set.intersection(*(set(frames[index]['metrics']) for index in measured))
        summaries={key:stats([frames[index]['metrics'][key] for index in measured]) for key in sorted(keys)}
        runs[stem]={'command':command,'csv_input':{'path':str(csv_path.resolve()),'sha256':sha(csv_path)},
                    'log_input':{'path':str(log_path.resolve()),'sha256':sha(log_path)},'frames':frames,
                    'measured_row_indices':measured,'process_stats':summaries,
                    'raw_events':events,'unassigned_after_last_action':[event for event in events if event['log_line_number']>previous],
                    'parser_warnings':warnings,'alignment_passed':True}
        if command.get('cpu_observation_input'):
            cpu_path=directory/(stem+'.cpu-observation.json')
            require(command['cpu_observation_input']['sha256']==sha(cpu_path),'CPU observation hash differs: '+stem)
            runs[stem]['cpu_observation']=read(cpu_path)
    comparisons={}
    variants=sorted({record['variant'] for record in manifest['commands']})
    for variant in variants:
        modes={mode:sorted((run for run in runs.values() if run['command']['variant']==variant and run['command']['mode']==mode),key=lambda run:run['command']['round']) for mode in ('on','off')}
        require(len(modes['on'])==len(modes['off'])==3,'Need three complete on/off rounds: '+variant)
        keys=set.intersection(*(set(run['process_stats']) for run in modes['on']+modes['off']))
        pairs=[]
        for off,on in zip(modes['off'],modes['on']):
            require(off['command']['round']==on['command']['round'],'Round mismatch')
            for record,mode in ((off['command'],'off'),(on['command'],'on')):
                require((record['environment'].get(OPTOUT)=='1') if mode=='off' else OPTOUT not in record['environment'],'Optout differs')
            comparable_env=lambda record:{key:value for key,value in record['environment'].items() if key!=OPTOUT}
            require(comparable_env(off['command'])==comparable_env(on['command']) and off['command']['source_content_revision']==on['command']['source_content_revision'],'Pair environment/source differs')
            pairs.append({'round':off['command']['round'],'off':off['command']['stem'],'on':on['command']['stem'],
                          'metrics':{key:diff(off['process_stats'][key]['median'],on['process_stats'][key]['median']) for key in sorted(keys)}})
        comparisons[variant]={'pairs':pairs,'median_of_process_medians':{key:diff(statistics.median(run['process_stats'][key]['median'] for run in modes['off']),statistics.median(run['process_stats'][key]['median'] for run in modes['on'])) for key in sorted(keys)},
            'process_median_ranges':{mode:{key:{'minimum':min(run['process_stats'][key]['median'] for run in selected),'maximum':max(run['process_stats'][key]['median'] for run in selected)} for key in sorted(keys)} for mode,selected in modes.items()}}
    counts={'processes':len(runs),'measured_frames':sum(len(run['measured_row_indices']) for run in runs.values()),
            'warmup_frames':sum(len(run['frames'])-len(run['measured_row_indices']) for run in runs.values())}
    require(counts==manifest['completed_unique_counts']==manifest['expected_unique_counts'],'Campaign counts incomplete')
    require(manifest.get('binaries_unchanged') is True and manifest['binary_hashes']==manifest['binary_hashes_post_campaign'],
            'Diagnostic binary hashes changed')
    return {'command_metadata':manifest,'parser_sha256':sha(trace_helper),'analyzer_sha256':sha(Path(__file__)),
            'unique_counts':counts,'runs':runs,'comparisons':comparisons,'issues':issues,'complete_and_comparable':True,
            'limitations':['Trace phases are nested; medians cannot be added. Row-wise residuals are descriptive.',
                'update_ms includes setters and synchronous notifications; no isolated notify timer exists.',
                'Action marker is emitted before commit/rememberFrameRoot; render_outside_action includes later work and trace I/O.',
                'No GPU timestamps/query drain; read_wait is wall time waiting for BGFX worker/GPU/readback, not isolated GPU time.',
                'All measured/warmup frames and positive pair deltas are retained; diagnostic tracing is not a quiet timing claim.']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('/tmp/coin-render-bgfx-regression-diagnostic'))
    parser.add_argument('--trace-helper',type=Path,default=Path('/tmp/coin-render-geometry-overlay-diagnostics.py'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();require(not args.output.exists(),'Output must be absent')
    result=analyze(args.input.resolve(),args.trace_helper.resolve())
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({'counts':result['unique_counts'],'complete':result['complete_and_comparable']},indent=2))


if __name__=='__main__':main()
