#!/usr/bin/env python3
"""Sequential, interleaved native/two-center/fine/fine-uniform controls."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--artifacts',type=Path,required=True)
p.add_argument('--kind',choices=['gpu','window','city'],required=True)
p.add_argument('--gpus',default='amd,nvidia');p.add_argument('--backends',default='wgpu,bgfx')
p.add_argument('--resume',action='store_true');p.add_argument('--modes',default='');p.add_argument('--workloads',default='');a=p.parse_args()
if a.kind != 'gpu':
 from legacy_runtime import require_legacy_sampling
 for backend in a.backends.split(','):require_legacy_sampling(a.artifacts/('build-'+backend))
out=a.artifacts/('benchmark-fine-'+a.kind);out.mkdir(exist_ok=True)
runs=json.loads((out/'summary.json').read_text()) if (out/'summary.json').exists() else []
modes=['native','center','fine','fine_uniform'];codes={'native':0,'center':3,'fine':5,'fine_uniform':6}
for gpu in a.gpus.split(','):
 env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/dikluwe/.Xauthority',WGPU_BACKEND='vulkan',COIN_BGFX_RENDERER='vulkan',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1')
 env['VK_DRIVER_FILES']=env['VK_ICD_FILENAMES']='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json')
 env['__GLX_VENDOR_LIBRARY_NAME']='nvidia' if gpu=='nvidia' else 'mesa'
 env['__EGL_VENDOR_LIBRARY_FILENAMES']='/usr/share/glvnd/egl_vendor.d/'+('10_nvidia.json' if gpu=='nvidia' else '50_mesa.json')
 if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
 else:env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
 for backend in (['egl'] if a.kind=='gpu' else a.backends.split(',')):
  workloads=[(f'{u}-{q}',u,q) for u in [1,4,8] for q in [0,1]] if a.kind=='gpu' else [(s,None,None) for s in (['city-40000','city-1000000'] if a.kind=='city' else ['texture-1-0.5','texture-8-0.5','texture-8-0.8','texture-8-npot-0.5'])]
  if a.workloads:workloads=[x for x in workloads if x[0] in a.workloads.split(',')]
  for workload,units,linear in workloads:
   selected=a.modes.split(',') if a.modes else ['baseline',*modes] if a.kind=='window' else modes
   order=['baseline','native','fine_uniform','fine_uniform','native','baseline'] if a.kind=='city' else selected+list(reversed(selected))
   for repeat,mode in enumerate(order):
    key=f'{gpu}-{backend}-{workload}-{repeat}-{mode}';log=out/(key+'.log')
    if a.resume and any(r['log']==log.name and r['exit']==0 for r in runs):continue
    env['COIN_SAMPLING_STUDY']=mode
    if a.kind=='gpu':cmd=[str(a.artifacts/'gpu-benchmark-fine'),str(codes[mode]),str(linear),str(units)]
    else:
     baselines={'wgpu':'/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/build-linux-wgpu','bgfx':'/mnt/Laranja/Git/externos/coin-render-artifacts/raster-study-20261007/build-bgfx'}
     build=Path(baselines[backend]) if mode=='baseline' else a.artifacts/'original-study' if mode=='original-native' else a.artifacts/('build-'+backend)
     env['LD_LIBRARY_PATH']=str(build/'lib')
     if mode=='original-native':env['COIN_SAMPLING_STUDY']='native'
     scene=Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/apk-memory-final')/(workload+'.iv') if a.kind=='city' else a.artifacts/'scenes-fine'/(workload+'.iv')
     cmd=[str(build/'bin'/'coin_render_window_benchmark'),'--backend',backend+'-vulkan','--scene',str(scene),'--width','1280','--height','720','--warmup','45','--frames','150','--samples-output',str(out/(key+'.csv'))]
     if a.kind=='city':cmd+=['--animation','static']
    if not Path(cmd[0]).is_file():raise FileNotFoundError('Build the configured baseline/study window benchmark first: '+cmd[0])
    monitor_before=monitor_after=None;woke_monitor=False
    if a.kind!='gpu':
     monitor_before=subprocess.check_output(['xset','q'],env=env,text=True).split('Monitor is ')[-1].splitlines()[0].strip()
     if monitor_before=='Off':
      subprocess.run(['xset','dpms','force','on'],env=env,check=True);woke_monitor=True
      monitor_before=subprocess.check_output(['xset','q'],env=env,text=True).split('Monitor is ')[-1].splitlines()[0].strip()
    with log.open('w') as f:
     try:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=200).returncode
     except subprocess.TimeoutExpired:code=124
    if a.kind!='gpu':monitor_after=subprocess.check_output(['xset','q'],env=env,text=True).split('Monitor is ')[-1].splitlines()[0].strip()
    content=log.read_text(errors='replace')
    runs.append(dict(monitor_before=monitor_before,monitor_after=monitor_after,woke_monitor=woke_monitor,timing_admitted=a.kind=='gpu' or monitor_before==monitor_after=='On',gpu=gpu,backend=backend,workload=workload,mode=mode,repeat=repeat,command=cmd,exit=code,log=log.name,binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),environment={k:v for k,v in env.items() if k in ['LD_LIBRARY_PATH','COIN_SAMPLING_STUDY','WGPU_BACKEND','COIN_BGFX_RENDERER','VK_DRIVER_FILES','__GLX_VENDOR_LIBRARY_NAME','__EGL_VENDOR_LIBRARY_FILENAMES','__NV_PRIME_RENDER_OFFLOAD','DISPLAY','XAUTHORITY','COIN_GLX_PIXMAP_DIRECT_RENDERING','COIN_GLXGLUE_NO_PBUFFERS']},gpu_ms=[float(x) for x in re.findall(r'gpu_ms=([0-9.]+)',content)],report=next((x for x in content.splitlines() if x.startswith('window_benchmark')),None)))
    (out/'summary.json').write_text(json.dumps(runs,indent=2));print(key,code,flush=True)
