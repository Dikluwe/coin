#!/usr/bin/env python3
import argparse,os,subprocess,json,hashlib,re
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--artifacts',type=Path,required=True);a=ap.parse_args();a.artifacts.mkdir(exist_ok=True);runs=[]
for gpu in ['amd','nvidia']:
 env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/dikluwe/.Xauthority');env['__EGL_VENDOR_LIBRARY_FILENAMES']='/usr/share/glvnd/egl_vendor.d/'+('10_nvidia.json' if gpu=='nvidia' else '50_mesa.json')
 if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
 else:env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
 for size,derived in [(n,False) for n in [128,256,512,1024,2048,4096,257,511]]+[(4096,True)]:
  key=f'{gpu}-{size}'+('-derived' if derived else '');log=a.artifacts/(key+'.log');cmd=[str(a.artifacts/'base-probe'),str(size),str(a.artifacts/key)]
  if derived:cmd.append('--derived')
  with log.open('w') as f:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=60).returncode
  text=log.read_text();cases=[]
  for line in text.splitlines():
   if line.startswith('case '):cases.append({k:(v if k=='mode' else float(v) if k in ['lod','mae'] else int(v)) for k,v in re.findall(r'(\w+)=([^ ]+)',line)})
  runs.append(dict(gpu=gpu,size=size,derived=derived,exit=code,command=cmd,environment={k:env[k] for k in ['DISPLAY','XAUTHORITY','__EGL_VENDOR_LIBRARY_FILENAMES','__NV_PRIME_RENDER_OFFLOAD'] if k in env},adapter=next((v for v in text.splitlines() if v.startswith('adapter=')),None),cases=cases,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest()))
  (a.artifacts/'summary.json').write_text(json.dumps(runs,indent=2)+'\n');print(key,code,flush=True)
