import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-state-ablation');out.mkdir(exist_ok=False)
build=Path('/tmp/coin-render-first-frame-wgpu')
metadata={'source_content_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'binary_hashes':{str(p):r.sha256(p) for p in [build/'bin/coin_render_gl_benchmark',build/'lib/libCoinRender.so',build/'lib/libCoin.so.80']},'scene_sha256':r.sha256(Path('/tmp/coin-render-city-40000.iv')),'commands':[]}
for case in ('transforms-10','materials-10','geometry-10'):
 for mode,memo_off,common_off in [('both-on',False,False),('composition-off',True,False),('common-state-off',False,True),('both-off',True,True)]:
  animation,percent=r.case_options(case);env=r.environment(build,'wgpu-vulkan','nvidia');env['COIN_RENDER_TRACE_PHASES']='1'
  if memo_off:env['COIN_RENDER_DISABLE_COMPOSITION_RANGE_MEMOIZATION']='1'
  if common_off:env['COIN_WGPU_DISABLE_INSTANCE_COMMON_STATE']='1'
  stem=case+'-'+mode
  cmd=[str(build/'bin/coin_render_gl_benchmark'),'--backend','wgpu','--scene','/tmp/coin-render-city-40000.iv','--animation',animation,'--animated-percent',str(percent),'--transparency','object','--size','1024','--warmup','5','--frames','3','--samples-output',str(out/(stem+'.csv'))]
  metadata['commands'].append({'stem':stem,'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_','__NV','__GLX','VK_','LD_LIBRARY'))}})
  (out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
  print('START',stem,flush=True)
  p=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180)
  (out/(stem+'.log')).write_text(p.stdout+p.stderr)
  print('END',stem,p.returncode,flush=True)
  if p.returncode:raise SystemExit(p.returncode)
