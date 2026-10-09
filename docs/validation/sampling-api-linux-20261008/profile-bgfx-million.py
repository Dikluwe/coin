import os,json,subprocess,hashlib,re,time
from pathlib import Path
r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux');out=r/'bgfx-million-profile';out.mkdir(exist_ok=True);repo=Path('/home/dikluwe/.codex/worktrees/coin-portable-sampling/coin')
base=json.loads((r/'benchmark-cpu-final/summary.json').read_text());rows=[]
for policy in ['native','portable']:
 prior=next(x for x in base if x['profile']=='bgfx-amd-vulkan' and x['workload']=='city-1000000' and x['policy']==policy)
 env=dict(os.environ);env.update(prior['environment']);env['COIN_RENDER_TRACE_PHASES']='1'
 for key in ['COIN_SAMPLING_AUDIT','COIN_WGPU_GPU_TIMESTAMPS']:env.pop(key,None)
 cmd=list(prior['command']);cmd[cmd.index('--frames')+1]='4';cmd[cmd.index('--warmup')+1]='1';cmd[cmd.index('--samples-output')+1]=str(out/(policy+'.csv'))
 log=out/(policy+'.log');start=time.time()
 with log.open('w') as f:code=subprocess.run(cmd,env=env,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=240).returncode
 text=log.read_text();phases=[];geometry=[]
 for line in text.splitlines():
  if line.startswith('COIN_RENDER_PHASE bgfx lower_ms='):phases.append(dict(re.findall(r'(\w+)=([^ ]+)',line)))
  if line.startswith('COIN_RENDER_PHASE bgfx_geometry '):geometry.append(dict(re.findall(r'(\w+)=([^ ]+)',line)))
 valid=code==0 and len(phases)==5 and len(geometry)==5 and 'vendor_id=0x1002' in text
 rows.append(dict(policy=policy,exit=code,valid=valid,command=cmd,environment={k:v for k,v in env.items() if k in prior['environment'] or k=='COIN_RENDER_TRACE_PHASES'},duration_s=time.time()-start,phases=phases,geometry=geometry,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),runtime_sha256=hashlib.sha256((r/'build-bgfx/lib/libCoinRender.so').read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),log=log.name))
 (out/'summary.json').write_text(json.dumps(rows,indent=2));print(policy,valid,flush=True)
assert all(x['valid'] for x in rows)
