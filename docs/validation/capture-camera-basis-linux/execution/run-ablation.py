import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame');flag='COIN_RENDER_DISABLE_CAPTURE_CAMERA_BASIS_REUSE'
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-capture-ablation');out.mkdir(exist_ok=False)
metadata={'source_content_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'scene_sha256':r.sha256(Path('/tmp/coin-render-city-40000.iv')),'binary_hashes':{},'commands':[]}
for round_index in range(3):
 variants=['wgpu-vulkan','bgfx-vulkan','bgfx-opengl'];variants=variants[round_index:]+variants[:round_index]
 for variant in variants:
  build=Path('/tmp/coin-render-first-frame-'+('wgpu' if variant.startswith('wgpu') else 'bgfx'))
  for path in (build/'bin/coin_render_gl_benchmark',build/'lib/libCoinRender.so',build/'lib/libCoin.so.80'):metadata['binary_hashes'][str(path)]=r.sha256(path)
  for disabled in ([False,True] if round_index%2==0 else [True,False]):
   mode='reuse-off' if disabled else 'reuse-on';stem=variant+'-static-'+mode+'-'+str(round_index+1)
   env=r.environment(build,variant,'nvidia');env['COIN_RENDER_TRACE_PHASES']='1'
   if disabled:env[flag]='1'
   command=[str(build/'bin/coin_render_gl_benchmark'),'--backend','wgpu' if variant.startswith('wgpu') else 'bgfx','--scene','/tmp/coin-render-city-40000.iv','--animation','static','--animated-percent','10','--transparency','object','--size','1024','--warmup','0','--frames','1','--samples-output',str(out/(stem+'.csv'))]
   item={'stem':stem,'variant':variant,'round':round_index+1,'mode':mode,'command':command,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_','__NV','__GLX','VK_','LD_LIBRARY'))}}
   metadata['commands'].append(item);(out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
   print('START',stem,flush=True)
   p=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True,timeout=180)
   log=p.stdout+p.stderr;(out/(stem+'.log')).write_text(log)
   item['exit_code']=p.returncode
   (out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
   if p.returncode:raise RuntimeError(stem+' failed '+log[-2000:])
   assert 'NVIDIA' in log,stem+' adapter not NVIDIA'
   item['adapter_verified_nvidia']=True;item['measured_csv_stats']=r.csv_stats(out/(stem+'.csv'),1)
   (out/'commands.json').write_text(json.dumps(metadata,indent=2)+'\n')
   print('END',stem,flush=True)
