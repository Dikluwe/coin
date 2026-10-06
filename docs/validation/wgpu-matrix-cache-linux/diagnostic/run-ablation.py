import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-matrix-ablation');out.mkdir(exist_ok=False)
build=Path('/tmp/coin-render-first-frame-wgpu')
metadata={'source_content_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'binary_hashes':{str(p):r.sha256(p) for p in [build/'bin/coin_render_gl_benchmark',build/'lib/libCoinRender.so',build/'lib/libCoin.so.80']},'scene_sha256':r.sha256(Path('/tmp/coin-render-city-40000.iv')),'commands':[]}
for variant,cases in [('wgpu-vulkan',('transforms-10','materials-10','geometry-10','transforms-100'))]:
 build=Path('/tmp/coin-render-first-frame-'+('wgpu' if variant.startswith('wgpu') else 'bgfx'))
 for p in (build/'bin/coin_render_gl_benchmark',build/'lib/libCoinRender.so',build/'lib/libCoin.so.80'):
  metadata['binary_hashes'][str(p)]=r.sha256(p)
 for case in cases:
  for mode,disabled in [('cache-on',False),('cache-off',True)]:
   animation,percent=r.case_options(case);env=r.environment(build,variant,'nvidia');env['COIN_RENDER_TRACE_PHASES']='1'
   if disabled:env['COIN_WGPU_DISABLE_INSTANCE_MATRIX_CACHE']='1'
   stem=variant+'-'+case+'-'+mode
   cmd=[str(build/'bin/coin_render_gl_benchmark'),'--backend','wgpu' if variant.startswith('wgpu') else 'bgfx','--scene','/tmp/coin-render-city-40000.iv','--animation',animation,'--animated-percent',str(percent),'--transparency','object','--size','1024','--warmup','5','--frames','3','--samples-output',str(out/(stem+'.csv'))]
   metadata['commands'].append({'stem':stem,'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_','__NV','__GLX','VK_','LD_LIBRARY'))}})
   (out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
   print('START',stem,flush=True)
   p=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180)
   (out/(stem+'.log')).write_text(p.stdout+p.stderr)
   metadata['commands'][-1]['exit_code']=p.returncode
   (out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
   print('END',stem,p.returncode,flush=True)
   if p.returncode:raise SystemExit(p.returncode)
   assert 'NVIDIA' in p.stdout+p.stderr, 'Unexpected adapter for '+stem
   metadata['commands'][-1]['adapter_verified_nvidia']=True
   metadata['commands'][-1]['measured_csv_stats']=r.csv_stats(out/(stem+'.csv'),3)
   (out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
