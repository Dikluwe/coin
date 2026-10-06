#!/usr/bin/env python3
"""Alternate composition-copy baselines/current variants with one shared CoinGL control.

The optional before-family revisions identify mixed frozen BGFX/wgpu builds.
Only main() launches processes; importing this tool never runs Git or a GPU.
"""
import argparse,copy,importlib.util,json,shutil,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True

def module(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['before-bgfx-build','after-bgfx-build','before-wgpu-build','after-wgpu-build','coingl-build','scene','output-before','output-after']:
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--runner',type=Path,default=Path('/tmp/coin-render-first-frame/scripts/coinrender/run_animation_benchmark.py'))
    p.add_argument('--row-helper',type=Path,default=Path('/tmp/coin-render-wgpu-motion-matched.py'))
    p.add_argument('--variants',default='coingl,bgfx-vulkan,bgfx-opengl,wgpu-vulkan')
    p.add_argument('--cases',default='materials-10,geometry-10')
    p.add_argument('--scope',choices=['offscreen','window'],default='offscreen')
    p.add_argument('--gpu',choices=['nvidia','amd'],default='nvidia')
    for name,default in [('rounds',3),('warmup',5),('frames',30),('size',1024),('timeout',1800)]:p.add_argument('--'+name,type=int,default=default)
    for name in ['before-source-content-revision','after-source-content-revision','control-source-content-revision']:p.add_argument('--'+name,required=True)
    for name in ['before-bgfx-source-content-revision','before-wgpu-source-content-revision']:
        p.add_argument('--'+name,default=None)
    a=p.parse_args();r=module('runner',a.runner);helper=module('matched_rows',a.row_helper)
    variants=a.variants.split(',');cases=a.cases.split(',')
    if not variants or variants.count('coingl')!=1 or len(set(variants))!=len(variants):p.error('Variants must include CoinGL exactly once')
    for v in variants:
        if v not in r.VARIANTS or (a.scope=='window' and not r.VARIANTS[v][2]):p.error('Unsupported variant/scope: '+v)
    for c in cases:r.case_options(c)
    if len(set(cases))!=len(cases) or not a.scene.is_file() or min(a.frames,a.rounds,a.size,a.timeout)<1 or a.warmup<0:p.error('Invalid cases/parameters')
    out={'before':a.output_before.resolve(),'after':a.output_after.resolve()}
    if out['before']==out['after'] or any(q.exists() for q in out.values()) or out['before'] in out['after'].parents or out['after'] in out['before'].parents:p.error('Use fresh separate output directories')
    builds={('control','coingl'):a.coingl_build.resolve()}
    for d in out:
        for v in variants:
            if v=='coingl':continue
            family='bgfx' if v.startswith('bgfx-') else 'wgpu'
            builds[(d,v)]=getattr(a,d+'_'+family+'_build').resolve()
    binary='coin_render_gl_benchmark' if a.scope=='offscreen' else 'coin_render_window_benchmark'
    hashes={}
    for key,b in builds.items():
        hashes[key]={}
        for q in [b/'bin'/binary,b/'lib/libCoinRender.so',b/'lib/libCoin.so.80']:
            if not q.is_file():p.error('Missing binary '+str(q))
            hashes[key][str(q)]=r.sha256(q)
    source={'before':a.before_source_content_revision,'after':a.after_source_content_revision,'control':a.control_source_content_revision}
    def command_source(role,variant):
        if role=='control' or variant=='coingl':return source['control']
        if role=='before':
            family='bgfx' if variant.startswith('bgfx-') else 'wgpu'
            return getattr(a,'before_'+family+'_source_content_revision') or source[role]
        return source[role]
    roles=[('control','coingl')]+[(d,v) for v in variants if v!='coingl' for d in out]
    params={k:str(v) if isinstance(v,Path) else v for k,v in vars(a).items()};params['mode']='measure'
    manifests={};rows={}
    root=a.runner.resolve().parents[2];snapshot=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    for d,q in out.items():
        q.mkdir();[(q/n).mkdir() for n in ['logs','samples','images']]
        h=dict(hashes[('control','coingl')])
        for v in variants:
            if v!='coingl':h.update(hashes[(d,v)])
        variant_sources={v:command_source(d,v) for v in variants}
        family_sources=set(variant_sources[v] for v in variants if v!='coingl')
        aggregate_source=next(iter(family_sources)) if len(family_sources)==1 else None
        manifests[d]={'parameters':dict(params,output=str(q)), 'scene_sha256':r.sha256(a.scene), 'binary_hashes':h,'commands':[], 'execution_order':[], 'source_revision':aggregate_source, 'source_content_revision':aggregate_source, 'source_content_revision_kind':'single' if len(family_sources)==1 else 'mixed', 'source_snapshot_revision':snapshot, 'variant_source_content_revisions':variant_sources,'runner_path':str(a.runner.resolve()),'runner_sha256':r.sha256(a.runner),'row_helper_path':str(a.row_helper.resolve()),'row_helper_sha256':r.sha256(a.row_helper),'matched_runner_sha256':r.sha256(Path(__file__)),'shared_control':True,'matched_role':d,'matched_peer_output':str(out['after' if d=='before' else 'before'])}
        rows[d]=[];r.write_json(q/'manifest.json',manifests[d]);r.write_json(q/'results.json',rows[d])
    index=0
    for round_index in range(a.rounds):
        ordered=cases[round_index%len(cases):]+cases[:round_index%len(cases)]
        if round_index%2:ordered.reverse()
        for case_index,c in enumerate(ordered):
            offset=(round_index+case_index)%len(roles);sequence=roles[offset:]+roles[:offset]
            if round_index%2:sequence.reverse()
            for d,v in sequence:
                index+=1;dest=list(out) if d=='control' else [d];executing='before' if d=='control' else d
                q=out[executing];b=builds[(d,v)];stem=f'{c}-{v}-{round_index+1}'
                samples=q/'samples'/(stem+'.csv');logfile=q/'logs'/(stem+'.log');animation,percent=r.case_options(c)
                backend=r.VARIANTS[v][1 if a.scope=='offscreen' else 2]
                cmd=[str(b/'bin'/binary),'--backend',backend,'--scene',str(a.scene.resolve()),'--animation',animation,'--animated-percent',str(percent),'--transparency','object','--warmup',str(a.warmup),'--frames',str(a.frames),'--samples-output',str(samples)]
                cmd+=['--size',str(a.size)] if a.scope=='offscreen' else ['--width',str(a.size),'--height',str(a.size)]
                env=r.environment(b,v,a.gpu);saved={k:env[k] for k in ['LD_LIBRARY_PATH','VK_ICD_FILENAMES','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','COIN_BGFX_RENDERER','WGPU_BACKEND','COIN_GLX_PIXMAP_DIRECT_RENDERING','COIN_GLXGLUE_NO_PBUFFERS'] if k in env}
                timed_cmd=['/usr/bin/time','-f','benchmark_peak_rss_kib=%M',*cmd]
                rec={'stem':stem,'command':cmd,'timed_command':timed_cmd,'environment':saved,'matched_role':d,'execution_index':index,'case':c,'variant':v,'round':round_index+1,'source_content_revision':command_source(d,v),'shared_control':d=='control'}
                order={'case':c,'variant':v,'round':round_index+1,'matched_role':d,'execution_index':index,'stem':stem}
                for output in out:
                    manifests[output]['execution_order'].append(order)
                    if output in dest:manifests[output]['commands'].append(copy.deepcopy(rec))
                    r.write_json(out[output]/'manifest.json',manifests[output])
                print('START',stem,d,index,flush=True)
                try:
                    result=subprocess.run(timed_cmd,env=env,capture_output=True,text=True,timeout=a.timeout)
                except subprocess.TimeoutExpired as error:
                    def as_text(value):return value.decode('utf-8',errors='replace') if isinstance(value,bytes) else (value or '')
                    logfile.write_text(as_text(error.stdout)+as_text(error.stderr))
                    for output in dest:
                        manifests[output]['commands'][-1].update(exit_code=None,timed_out=True,timeout_seconds=a.timeout)
                        r.write_json(out[output]/'manifest.json',manifests[output])
                    raise
                log=result.stdout+result.stderr;logfile.write_text(log)
                for output in dest:
                    manifests[output]['commands'][-1].update(exit_code=result.returncode,timed_out=False)
                    r.write_json(out[output]/'manifest.json',manifests[output])
                if result.returncode:raise RuntimeError(f'{stem}/{d} return={result.returncode}: {log[-2000:]}')
                if v!='coingl' and a.gpu=='nvidia':assert 'NVIDIA' in log,(stem,d)
                row=helper.row_from_log(r,log,samples,case=c,variant=v,round_number=round_index+1,args=a)
                row['source_content_revision']=command_source(d,v)
                if d=='control':
                    for n,file in [('logs',logfile),('samples',samples)]:
                        dst=out['after']/n/file.name;shutil.copy2(file,dst);assert r.sha256(file)==r.sha256(dst)
                for output in dest:rows[output].append(copy.deepcopy(row));r.write_json(out[output]/'results.json',rows[output])
                print('DONE',stem,d,'median',row['stats']['total_ms']['median_ms'],'p95',row['stats']['total_ms']['p95_ms'],flush=True)
    helper.VARIANTS=tuple(variants)
    for d,q in out.items():helper.write_medians(r,q,rows[d],cases,a);assert not list((q/'images').iterdir())
    print('Completed',index,'processes',flush=True)
if __name__=='__main__':main()
