#!/usr/bin/env python3
"""Fresh-process native FreeCAD AssemblyExample effects benchmark.

Samples actual forced static repaints, independently for each effects mode.
Linux smaps PSS/RSS includes driver allocations in that process; BGFX texture
estimates are reported separately and must not be added to process PSS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import signal
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def summarize(results):
    modes={}
    for mode in ('off','shadows','ao','both'):
        runs=[r for r in results if r['mode']==mode]
        values=[x['render_ms'] for r in runs for x in r['samples']]
        acks=[x['ack_ms'] for r in runs for x in r['samples']]
        ordered=sorted(values)
        first=runs[0]['samples'][0]
        modes[mode]=dict(runs=len(runs),samples=len(values),render_median_ms=statistics.median(values),
            render_p95_ms=ordered[min(len(ordered)-1,int(len(ordered)*.95))],ack_median_ms=statistics.median(acks),
            run_render_medians_ms=[statistics.median(x['render_ms'] for x in r['samples']) for r in runs],
            cold_render_median_ms=statistics.median(r['cold']['render_ms'] for r in runs),
            worker_pss_median_bytes=statistics.median(r['memory_end']['worker']['Pss_bytes'] for r in runs),
            worker_rss_median_bytes=statistics.median(r['memory_end']['worker']['Rss_bytes'] for r in runs),
            freecad_pss_median_bytes=statistics.median(r['memory_end']['host']['Pss_bytes'] for r in runs),
            total_pss_median_bytes=statistics.median(sum(m['Pss_bytes'] for m in r['memory_end'].values()) for r in runs),
            effect_attachment_bytes=first['effect_attachment_bytes'],
            bgfx_render_target_bytes=first['render_target_bytes'],bgfx_texture_bytes=first['texture_bytes'],
            warm_to_end_worker_pss_bytes=[r['memory_end']['worker']['Pss_bytes']-r['memory_warm']['worker']['Pss_bytes'] for r in runs],
            captures=first['captures'],plans=first['plans'],transport_builds=first['transport_builds'],
            shadow_passes=first['shadow_passes'],ao_depth_passes=first['ao_depth_passes'],
            static_reuses=runs[-1]['samples'][-1].get('static_reuses',0))
    return modes


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--freecad',type=Path,required=True)
    parser.add_argument('--worker',type=Path,required=True)
    parser.add_argument('--environment',type=Path,required=True)
    parser.add_argument('--assembly',type=Path,required=True)
    parser.add_argument('--library',type=Path,help='Loaded CoinRender library; defaults to the worker build lib directory')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--qt-root',type=Path)
    parser.add_argument('--repeats',type=int,default=3)
    parser.add_argument('--samples',type=int,default=40)
    parser.add_argument('--warmup',type=int,default=8)
    parser.add_argument('--expect-static-reuse',action='store_true')
    args=parser.parse_args()
    assert args.repeats>0 and args.samples>=10 and args.warmup>=1
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    order=[]
    rng=random.Random(20261029)
    for run in range(args.repeats):
        modes=['off','shadows','ao','both'];rng.shuffle(modes)
        order.extend((run,mode) for mode in modes)
    results=[]
    camera=None;geometry_hash=None;packet_basis=None
    for run,mode in order:
        directory=output/f'{run}-{mode}';directory.mkdir()
        env=os.environ.copy()
        env.update(COIN_VIEWPORT_SOURCE=str(ROOT),COIN_VIEWPORT_WORKER=str(args.worker.resolve()),
            COIN_VIEWPORT_ENVIRONMENT=str(args.environment.resolve()),COIN_VIEWPORT_ASSEMBLY=str(args.assembly.resolve()),
            COIN_BENCH_OUTPUT=str(directory),COIN_BENCH_MODE=mode,COIN_BENCH_SAMPLES=str(args.samples),
            COIN_BENCH_WARMUP=str(args.warmup),COIN_BENCH_EXPECT_REUSE=str(int(args.expect_static_reuse)),QT_QPA_PLATFORM='xcb')
        env.pop('FREECAD_COIN_WGPU',None)
        if args.qt_root:
            env['LD_LIBRARY_PATH']=str(args.qt_root/'lib');env['QT_PLUGIN_PATH']=str(args.qt_root/'plugins')
        command=['xvfb-run','-a','-e',str(directory/'xvfb.log'),'-s','-screen 0 1680x1200x24',
            str(args.freecad.resolve()),'--user-cfg',str(directory/'user.cfg'),'--system-cfg',str(directory/'system.cfg'),
            str(ROOT/'examples/coinrender/freecad_assembly_effects_benchmark.FCMacro')]
        with (directory/'gui.log').open('w') as log:
            process=subprocess.Popen(command,env=env,stdin=subprocess.DEVNULL,stdout=log,
                                     stderr=subprocess.STDOUT,start_new_session=True)
            try:
                process.wait(timeout=150)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGTERM)
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid,signal.SIGKILL)
                    process.wait()
                raise
        assert process.returncode==0, directory/'gui.log'
        record=json.loads((directory/'result.json').read_text());assert record['status']=='PASS'
        record['run']=run
        scene_hash=sha(directory/'session/scene.iv')
        basis=(directory/'request.txt').read_text().split(' FX ')[0].split()[2:]
        if camera is None:
            camera=record['camera'];geometry_hash=scene_hash;packet_basis=basis
        assert record['camera']==camera and scene_hash==geometry_hash and basis==packet_basis, 'Scene/camera/pose mismatch'
        results.append(record)
        (output/'runs.json').write_text(json.dumps(results,indent=2)+'\n')
        print(f'{run}-{mode}: median {statistics.median(s["render_ms"] for s in record["samples"]):.3f} ms',flush=True)
    result=dict(status='PASS',modes=summarize(results),camera=camera,scene_sha256=geometry_hash,
        worker_sha256=sha(args.worker),assembly_sha256=sha(args.assembly),environment_sha256=sha(args.environment),
        viewport=[results[0]['cold']['width'],results[0]['cold']['height']],
        order=order,method='Fresh FreeCAD and worker for every mode/repetition; identical scene/camera/poses/materials/IBL; contiguous forced static repaints after warmup; cold submit reported separately. Native Vulkan, zero renderer/Qt pixel transport. No screenshot or memory sampling inside render_ms. PSS is read at warmup/end, host and worker separately. Timing is submission wall time/backpressure; unchanged repaints may pipeline presentation, changed frames retain their completion fence. This is not a GPU timestamp, display latency or measured display FPS.',
        library_sha256=sha(args.library or args.worker.resolve().parent.parent/'lib/libCoinRender.so'))
    (output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['modes'],indent=2))


if __name__=='__main__':main()
