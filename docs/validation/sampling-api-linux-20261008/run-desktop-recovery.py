import json,subprocess,time
from pathlib import Path
repo=Path('/home/dikluwe/.codex/worktrees/coin-portable-sampling/coin');r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux');rows=[]
commands=[
 ['python3','testsuite/reproducers/sampling-api/qualify_freecad_linux.py','--artifacts',str(r),'--name','freecad-dpr1-desktop-warm-recovery','--profiles','bgfx-nvidia-vulkan','--policies','portable,native'],
 ['python3','testsuite/reproducers/sampling-api/qualify_freecad_linux.py','--artifacts',str(r),'--name','freecad-dpr2-desktop-warm-final','--profiles','bgfx-nvidia-vulkan,bgfx-amd-gl,wgpu-nvidia-vulkan,wgpu-amd-gl','--policies','portable','--scale','2'],
 ['python3','testsuite/reproducers/sampling-api/run.py','--artifacts',str(r),'--name','diagnostic-final-api-window','--cases','api,window']]
for i,cmd in enumerate(commands):
 log=r/f'desktop-recovery-{i}.log';start=time.time()
 with log.open('w') as f:code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT).returncode
 rows.append(dict(command=cmd,exit=code,duration_s=time.time()-start,log=log.name));(r/'desktop-recovery-summary.json').write_text(json.dumps(rows,indent=2));print(i,code,flush=True)
 if code:raise SystemExit(code)
