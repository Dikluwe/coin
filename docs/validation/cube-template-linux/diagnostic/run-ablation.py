import importlib.util, json, subprocess, sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-cube-ablation');out.mkdir(exist_ok=False)
build=Path('/tmp/coin-render-first-frame-wgpu');variant='wgpu-vulkan'
metadata={'source_content_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'binary_hashes':{str(p):r.sha256(p) for p in [build/'bin/coin_render_gl_benchmark',build/'lib/libCoinRender.so',build/'lib/libCoin.so.80']},'scene_sha256':r.sha256(Path('/tmp/coin-render-city-40000.iv')),'commands':[]}
for case in ('geometry-10','static'):
 for mode,legacy,memo_off in [('both-on',False,False),('legacy-template',True,False),('memo-off',False,True),('both-off',True,True)]:
  animation,percent=r.case_options(case);env=r.environment(build,variant,'nvidia');env['COIN_RENDER_TRACE_PHASES']='1'
  if legacy:env['COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE']='1'
  if memo_off:env['COIN_RENDER_DISABLE_OBJECT_PROOF_MEMOIZATION']='1'
  stem=case+'-'+mode
  cmd=[str(build/'bin/coin_render_gl_benchmark'),'--backend','wgpu','--scene','/tmp/coin-render-city-40000.iv','--animation',animation,'--animated-percent',str(percent),'--transparency','object','--size','1024','--warmup','5','--frames','3','--samples-output',str(out/(stem+'.csv'))]
  metadata['commands'].append({'stem':stem,'command':cmd,'environment':{k:env[k] for k in ('LD_LIBRARY_PATH','COIN_RENDER_TRACE_PHASES','COIN_RENDER_DISABLE_CUBE_TEMPLATE_CACHE','COIN_RENDER_DISABLE_OBJECT_PROOF_MEMOIZATION','VK_ICD_FILENAMES','WGPU_BACKEND','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME') if k in env}})
  (out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
  print('START',stem,flush=True)
  result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180)
  (out/(stem+'.log')).write_text(result.stdout+result.stderr)
  print('END',stem,result.returncode,flush=True)
  if result.returncode:raise SystemExit(result.returncode)
