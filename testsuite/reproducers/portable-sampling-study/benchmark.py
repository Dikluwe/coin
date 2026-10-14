#!/usr/bin/env python3
import argparse,os,subprocess,json,hashlib,re
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--artifacts',type=Path,required=True);ap.add_argument('--kind',choices=['gpu','window'],required=True);ap.add_argument("--optimized",action="store_true");ap.add_argument("--workloads",default="");a=ap.parse_args()
if a.kind == 'window':
 from legacy_runtime import require_legacy_sampling
 require_legacy_sampling(a.artifacts/'build-wgpu')
out=a.artifacts/('benchmark-'+a.kind+('-optimized' if a.optimized else ''));out.mkdir(exist_ok=True);runs=[]
for gpu in ['amd','nvidia']:
 env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/dikluwe/.Xauthority',WGPU_BACKEND='vulkan',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1');env['VK_DRIVER_FILES']=env['VK_ICD_FILENAMES']='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json');env['__GLX_VENDOR_LIBRARY_NAME']='nvidia' if gpu=='nvidia' else 'mesa';env['__EGL_VENDOR_LIBRARY_FILENAMES']='/usr/share/glvnd/egl_vendor.d/'+('10_nvidia.json' if gpu=='nvidia' else '50_mesa.json')
 if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
 else:env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
 workloads=[(str(u)+'-'+str(q),u,q) for u in [1,4,8] for q in [0,1]] if a.kind=='gpu' else [(f'texture-{u}-{q}',None,None) for u in [1,4,8] for q in [.5,.8]]+[('city-40000',None,None),('city-1000000',None,None)]
 if a.workloads:workloads=[x for x in workloads if x[0] in a.workloads.split(",")]
 for workload,u,q in workloads:
  order=([0,3,4,4,3,0] if a.optimized else [0,2,1,1,2,0]) if a.kind=='gpu' else (['native','center','fetch','fetch','center','native'] if a.optimized else ['native','fetch','fetch','native'])
  if a.kind=='window' and workload.startswith('city'):order=['baseline','native','fetch','fetch','native','baseline']
  for repeat,mode in enumerate(order):
   key=f'{gpu}-{workload}-{repeat}-{mode}';log=out/(key+'.log');env['COIN_SAMPLING_STUDY']=str(mode)
   if a.kind=='gpu':cmd=[str(a.artifacts/('gpu-benchmark-optimized' if a.optimized else 'gpu-benchmark')),str(mode),str(q),str(u)]
   else:
    build=Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/build-linux-wgpu') if mode=='baseline' else a.artifacts/'build-wgpu'
    scene=Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/apk-memory-final')/(workload+'.iv') if workload.startswith('city') else a.artifacts/'scenes'/(workload+'.iv')
    cmd=[str(build/'bin'/'coin_render_window_benchmark'),'--backend','wgpu-vulkan','--scene',str(scene),'--width','1280','--height','720','--warmup','45','--frames','150','--samples-output',str(out/(key+'.csv'))]
    if workload.startswith('city'):cmd+=['--animation','static']
   with log.open('w') as f:
    try:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=200).returncode
    except subprocess.TimeoutExpired:code=124
   text=log.read_text(errors='replace');runs.append(dict(gpu=gpu,workload=workload,mode=mode,repeat=repeat,command=cmd,exit=code,log=log.name,binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),gpu_ms=[float(x) for x in re.findall(r'gpu_ms=([0-9.]+)',text)],report=next((x for x in text.splitlines() if x.startswith('window_benchmark')),None)))
   (out/'summary.json').write_text(json.dumps(runs,indent=2));print(key,code,flush=True)
