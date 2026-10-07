"""Separate correctness and intrusive diagnostics after the timed campaign."""
import os, sys, json, subprocess, statistics, re
from pathlib import Path
repo=Path('/tmp/coin-render-first-frame')
sys.path.insert(0,str(repo/'scripts/coinrender'))
import run_performance_continuation as runner
import run_p18_profile as profile
runner.configure_variants()
env=dict(os.environ,DISPLAY=':0',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json')
import analyze_animation_images as images
for scope in ('offscreen',):
 directory=Path('/tmp/coin-perf-verify'+('-window' if scope=='window' else ''))
 cmd=['python3','scripts/coinrender/run_performance_continuation.py','--bgfx-build','/tmp/coin-perf-final/bgfx','--wgpu-build','/tmp/coin-perf-final/wgpu','--coingl-build','/tmp/coin-perf-baseline/bgfx','--scene','/tmp/coin-perf-final/city.iv','--scope',scope,'--mode','verify','--gpu','nvidia','--size','512','--output',str(directory)]
 subprocess.run(cmd,cwd=repo,env=env,check=True)
 subprocess.run(['python3','scripts/coinrender/analyze_animation_images.py',str(directory)],cwd=repo,check=True)
 rows=json.loads((directory/'results.json').read_text());pairs=[]
 for case in sorted({r['case'] for r in rows}):
  for api in ('bgfx-vulkan','bgfx-opengl','wgpu-vulkan'):
   a=images.captures(directory,next(r for r in rows if r['case']==case and r['variant']==api+'-literal'))
   b=images.captures(directory,next(r for r in rows if r['case']==case and r['variant']==api+'-reserve'))
   for frame in a:
    assert a[frame]['state']==b[frame]['state']
    assert a[frame]['image'].read_bytes()==b[frame]['image'].read_bytes(),(scope,case,api,frame)
    pairs.append(dict(case=case,api=api,frame=frame,exact_ppm=True))
 (directory/'exact-ab.json').write_text(json.dumps(pairs,indent=2)+'\n')
 print('Exact A/B images:',scope,len(pairs),flush=True)
directory=Path('/tmp/coin-perf-verify-window');directory.mkdir(exist_ok=True);rows=[];pairs=[]
for case in runner.DEFAULT_CASES.split(','):
 animation,percent=runner.animation.case_options(case)
 for api in ('bgfx-vulkan','bgfx-opengl','wgpu-vulkan'):
  for frames in (1,7):
   pair=[]
   for choice in ('literal','reserve'):
    variant=api+'-'+choice;key,_,backend,_=runner.animation.VARIANTS[variant];build=Path('/tmp/coin-perf-final')/key
    child=runner.environment(build,variant,'nvidia');child['DISPLAY']=':0'
    stem=f'{case}-{variant}-{frames}'
    cmd=[str(build/'bin/coin_render_window_benchmark'),'--backend',backend,'--scene','/tmp/coin-perf-final/city.iv','--animation',animation,'--animated-percent',str(percent),'--animation-step','100','--warmup','0','--frames',str(frames),'--width','512','--height','512','--capture-window']
    print('Window digest',stem,flush=True)
    p=subprocess.run(cmd,env=child,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    (directory/(stem+'.log')).write_text(p.stdout);assert p.returncode==0,(stem,p.returncode,p.stdout[-2000:])
    assert 'NVIDIA' in p.stdout
    rgba=re.search(r'window_rgba_fnv64=(\S+)',p.stdout)[1]
    state=re.search(r'final_state_digest=(\S+)',p.stdout)[1]
    row=dict(case=case,api=api,choice=choice,frames=frames,logical_frame=(frames-1)*100,rgba_fnv64=rgba,state=state,log=stem+'.log',command=cmd,
     environment={k:child[k] for k in ('DISPLAY','LD_LIBRARY_PATH','COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE','__EGL_VENDOR_LIBRARY_FILENAMES','VK_DRIVER_FILES')})
    rows.append(row);pair.append(row)
   assert pair[0]['rgba_fnv64']==pair[1]['rgba_fnv64'] and pair[0]['state']==pair[1]['state'],(case,api,frames)
   pairs.append(dict(case=case,api=api,logical_frame=(frames-1)*100,same_rgba_digest=True,state=state,rgba_fnv64=rgba))
   (directory/'results.json').write_text(json.dumps(rows,indent=2)+'\n');(directory/'exact-ab.json').write_text(json.dumps(pairs,indent=2)+'\n')
for case in runner.DEFAULT_CASES.split(','):
 for api in ('bgfx-vulkan','bgfx-opengl','wgpu-vulkan'):
  samples=[r for r in rows if r['case']==case and r['api']==api and r['choice']=='reserve']
  assert (samples[0]['rgba_fnv64']==samples[1]['rgba_fnv64'])==(case=='static'),(case,api,'window motion')
  assert (samples[0]['state']==samples[1]['state'])==(case=='static'),(case,api,'window state')
print('Window digest pairs:',len(pairs),'(not exported pixel equality)',flush=True)
for case in runner.DEFAULT_CASES.split(','):
 for frame in (0,600):assert len({r['state'] for r in pairs if r['case']==case and r['logical_frame']==frame})==1
out=Path('/tmp/coin-perf-final-diagnostics');out.mkdir(exist_ok=True);results=[]
for scope in ('offscreen','window'):
 for case in ('geometry-10','geometry-100'):
  for api in ('bgfx-vulkan','bgfx-opengl','wgpu-vulkan'):
   for choice in ('literal','reserve'):
    variant=api+'-'+choice;key,off,win,_=runner.animation.VARIANTS[variant];build=Path('/tmp/coin-perf-final')/key
    child=runner.environment(build,variant,'nvidia')
    child.update({k:env[k] for k in ('DISPLAY','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','VK_ICD_FILENAMES','VK_DRIVER_FILES','__EGL_VENDOR_LIBRARY_FILENAMES')})
    child.update(COIN_RENDER_TRACE_PHASES='1',COIN_WGPU_GPU_TIMESTAMPS='1',COIN_BGFX_TRACE_GL_ADAPTER='1')
    stem=f'{scope}-{case}-{variant}'
    cmd=[str(build/'bin'/('coin_render_gl_benchmark' if scope=='offscreen' else 'coin_render_window_benchmark')),'--backend',off if scope=='offscreen' else win,'--scene','/tmp/coin-perf-final/city.iv','--animation','geometry','--animated-percent',case.split('-')[1],'--warmup','5','--frames','15','--samples-output',str(out/(stem+'.csv'))]
    cmd+=['--size','512'] if scope=='offscreen' else ['--width','512','--height','512']
    print('Diagnostic',stem,flush=True)
    p=subprocess.run(cmd,env=child,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    log=p.stdout;(out/(stem+'.log')).write_text(log);assert p.returncode==0,(stem,p.returncode,log[-2000:])
    storage=profile.records(log,'object_update_storage')[-15:];assert len(storage)==15
    for s in storage:
     assert int(s['position_bytes'])<=16*1024*1024
     if choice=='reserve':assert s['position_grew']=='0' and s['draw_grew']=='0'
    phases=profile.records(log,'object_update_prepare')[-15:]
    row=dict(scope=scope,case=case,api=api,choice=choice,command=cmd,log=stem+'.log',storage=storage,
      preparation={k:profile.metric(phases,k) for k in phases[-1]})
    try:row['profile']=profile.summarize(log,api,scope,5,15)
    except ValueError as e:row['profile_limitation']=str(e)
    results.append(row);(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('Diagnostic processes:',len(results),flush=True)
