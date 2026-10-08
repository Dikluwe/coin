#!/usr/bin/env python3
"""Balanced NVIDIA million control, telemetry and separate diagnostic runs."""
import argparse,csv,hashlib,itertools,json,os,statistics,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--artifacts',type=Path,required=True);p.add_argument('--phase',default='before');p.add_argument('--candidate-build',type=Path);p.add_argument('--short',action='store_true');p.add_argument('--resume',action='store_true');a=p.parse_args()
out=a.artifacts/('million-'+a.phase);out.mkdir(exist_ok=True);runs=json.loads((out/'summary.json').read_text()) if a.resume and (out/'summary.json').exists() else []
env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/dikluwe/.Xauthority',WGPU_BACKEND='vulkan',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1')
for k in ['COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS']:env.pop(k,None)
original=a.artifacts/'original-study';candidate=a.candidate_build or original
builds={'baseline':Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/build-linux-wgpu'),'native':candidate,'fine_uniform':candidate}
scene=Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/apk-memory-final/city-1000000.iv')
orders=list(itertools.permutations(['baseline','native','fine_uniform']))
if a.short:orders=[orders[0],orders[-1]]
for group,order in enumerate(orders):
 for slot,mode in enumerate(order):
  key=f'{group}-{slot}-{mode}'
  previous=next((r for r in runs if r['group']==group and r['slot']==slot),None)
  if previous and previous['exit']==0 and previous['monitor_on']:continue
  if previous:
   rejected=out/'rejected-monitor-off'/key;rejected.mkdir(parents=True,exist_ok=False)
   (rejected/'row.json').write_text(json.dumps(previous,indent=2))
   for name in ['log','csv','telemetry']:
    (out/previous[name]).rename(rejected/previous[name])
   runs.remove(previous)
  child=dict(env,COIN_SAMPLING_STUDY=mode,LD_LIBRARY_PATH=str(builds[mode]/'lib'))
  before=subprocess.check_output(['xset','q'],env=child,text=True)
  if 'Monitor is Off' in before:subprocess.run(['xset','dpms','force','on'],env=child,check=True)
  before=subprocess.check_output(['xset','q'],env=child,text=True)
  cmd=[str(builds[mode]/'bin/coin_render_window_benchmark'),'--backend','wgpu-vulkan','--scene',str(scene),'--width','1280','--height','720','--warmup','45','--frames','150','--animation','static','--samples-output',str(out/(key+'.csv'))]
  with (out/(key+'-telemetry.csv')).open('w') as telemetry,(out/(key+'.log')).open('w') as log:
   probe=subprocess.Popen(['nvidia-smi','--query-gpu=timestamp,name,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu,memory.used','--format=csv','--loop-ms=1000'],stdout=telemetry,stderr=subprocess.STDOUT,env=child)
   start=time.monotonic()
   try:code=subprocess.run(cmd,env=child,stdout=log,stderr=subprocess.STDOUT,timeout=180).returncode
   except subprocess.TimeoutExpired:code=124
   finally:probe.terminate();probe.wait(timeout=10)
  after=subprocess.check_output(['xset','q'],env=child,text=True)
  row=dict(group=group,slot=slot,mode=mode,command=cmd,exit=code,seconds=time.monotonic()-start,monitor_on='Monitor is On' in before and 'Monitor is On' in after,library_path=child['LD_LIBRARY_PATH'],log=key+'.log',csv=key+'.csv',telemetry=key+'-telemetry.csv',binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest())
  if code==0:
   with (out/row['csv']).open() as f:samples=[float(x['render_present_ms']) for x in csv.DictReader(f) if x['warmup']=='0']
   row.update(median_ms=statistics.median(samples),p95_ms=sorted(samples)[int(.95*(len(samples)-1))])
  
  if previous:row['replaces_rejected_trial']=str(rejected.relative_to(out))
  runs.append(row);(out/'summary.json').write_text(json.dumps(runs,indent=2));print(key,code,row.get('median_ms'),flush=True)
