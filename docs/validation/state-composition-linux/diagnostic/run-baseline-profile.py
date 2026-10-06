import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-state-profile-before');out.mkdir(exist_ok=False)
meta={'source_content_revision':'9737b2e1e60eba6b44988d684c2838271d99dc05','scene_sha256':r.sha256(Path('/tmp/coin-render-city-40000.iv')),'commands':[],'binary_hashes':{}}
for variant,cases in [('wgpu-vulkan',('transforms-10','materials-10','geometry-10','static','camera')),('bgfx-vulkan',('transforms-10','geometry-10'))]:
 role='wgpu' if variant.startswith('wgpu') else 'bgfx';build=Path('/tmp/coin-render-state-baseline')/role
 for p in [build/'bin/coin_render_gl_benchmark',build/'lib/libCoinRender.so',build/'lib/libCoin.so.80']:
  meta['binary_hashes'][str(p)]=r.sha256(p)
 for case in cases:
  animation,percent=r.case_options(case);env=r.environment(build,variant,'nvidia');env['COIN_RENDER_TRACE_PHASES']='1'
  stem=variant+'-'+case
  cmd=[str(build/'bin/coin_render_gl_benchmark'),'--backend',role,'--scene','/tmp/coin-render-city-40000.iv','--animation',animation,'--animated-percent',str(percent),'--transparency','object','--size','1024','--warmup','5','--frames','3','--samples-output',str(out/(stem+'.csv'))]
  meta['commands'].append({'stem':stem,'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_','__NV','__GLX','VK_','LD_LIBRARY'))}})
  (out/'commands.json').write_text(json.dumps(meta,indent=2)+'\n')
  print('START',stem,flush=True)
  p=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180)
  (out/(stem+'.log')).write_text(p.stdout+p.stderr)
  print('END',stem,p.returncode,flush=True)
  if p.returncode:raise SystemExit(p.returncode)
