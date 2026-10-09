import json,subprocess,time
from pathlib import Path
repo=Path('/home/dikluwe/.codex/worktrees/coin-portable-sampling/coin');r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux')
prefix=['python3','testsuite/reproducers/sampling-api/'];commands=[
 ['python3','testsuite/reproducers/sampling-api/qualify_freecad_linux.py','--artifacts',str(r),'--name','freecad-warm-qualified-final','--policies','portable,native','--profiles','wgpu-nvidia-vulkan,bgfx-amd-gl,wgpu-amd-gl,bgfx-amd-vulkan,wgpu-amd-vulkan,bgfx-nvidia-vulkan'],
 ['python3','testsuite/reproducers/sampling-api/qualify_freecad_linux.py','--artifacts',str(r),'--name','freecad-dpr2-warm-qualified-final','--scale','2','--policies','portable'],
 ['python3','testsuite/reproducers/sampling-api/run.py','--artifacts',str(r),'--name','wgpu-diagnostic-qualified-final','--profiles','wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-gl','--cases','api,window'],
 ['python3','testsuite/reproducers/sampling-api/benchmark_linux.py','--artifacts',str(r),'--name','benchmark-gl-receipt-final','--profiles','bgfx-amd-gl'],
 ['python3','testsuite/reproducers/sampling-api/benchmark_linux.py','--artifacts',str(r),'--name','benchmark-gl-baseline-receipt-final','--profiles','bgfx-amd-gl','--baseline'],
 ['python3','testsuite/reproducers/sampling-api/benchmark_linux.py','--artifacts',str(r),'--name','benchmark-nvidia-million-repeat','--profiles','wgpu-nvidia-vulkan','--workloads','city-1000000','--frames','120','--warmup','30']]
rows=[]
for i,cmd in enumerate(commands[3:],3):
 start=time.time();log=r/f'closure-performance-final-{i}.log'
 with log.open('w') as f: code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT).returncode
 rows.append(dict(index=i,command=cmd,exit=code,start=start,duration_s=time.time()-start,log=log.name))
 (r/'closure-performance-final-summary.json').write_text(json.dumps(rows,indent=2));print(i,code,flush=True)
 if code:raise SystemExit(code)
