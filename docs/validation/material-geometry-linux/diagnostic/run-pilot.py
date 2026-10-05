import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-material-geometry-pilot');out.mkdir(exist_ok=True)
commands=[]
for case in ('materials-10','geometry-10','materials-100','geometry-100'):
 for variant in ('wgpu-vulkan','bgfx-vulkan','bgfx-opengl'):
  build=Path('/tmp/coin-render-first-frame-'+('wgpu' if variant.startswith('wgpu') else 'bgfx'))
  mode,percent=r.case_options(case);env=r.environment(build,variant,'nvidia');env['COIN_RENDER_TRACE_PHASES']='1'
  stem=case+'-'+variant
  cmd=[str(build/'bin/coin_render_gl_benchmark'),'--backend',r.VARIANTS[variant][1],'--scene','/tmp/coin-render-city-40000.iv','--animation',mode,'--animated-percent',str(percent),'--transparency','object','--size','1024','--warmup','5','--frames','3','--samples-output',str(out/(stem+'.csv'))]
  commands.append({'command':cmd,'environment':{k:env[k] for k in ('LD_LIBRARY_PATH','COIN_RENDER_TRACE_PHASES','VK_ICD_FILENAMES','COIN_BGFX_RENDERER','WGPU_BACKEND','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME') if k in env}})
  (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  print('START',stem,flush=True)
  result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=240);(out/(stem+'.log')).write_text(result.stdout+result.stderr)
  print('END',stem,result.returncode,flush=True)
  if result.returncode:raise SystemExit(result.returncode)
