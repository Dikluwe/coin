import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame');spec=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
out=Path('/tmp/coin-render-material-index-ablation');out.mkdir(exist_ok=True);commands=[]
for disabled in (False,True):
 name='index-off' if disabled else 'index-on';build=Path('/tmp/coin-render-first-frame-wgpu');env=r.environment(build,'wgpu-vulkan','nvidia');env['COIN_RENDER_TRACE_PHASES']='1'
 if disabled:env['COIN_RENDER_DISABLE_MATERIAL_INTERNING']='1'
 cmd=[str(build/'bin/coin_render_gl_benchmark'),'--backend','wgpu','--scene','/tmp/coin-render-city-40000.iv','--animation','materials','--animated-percent','100','--transparency','object','--size','1024','--warmup','0','--frames','2','--samples-output',str(out/(name+'.csv'))]
 commands.append({'command':cmd,'environment':{k:v for k,v in env.items() if k in ('LD_LIBRARY_PATH','VK_ICD_FILENAMES','WGPU_BACKEND','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','COIN_RENDER_TRACE_PHASES','COIN_RENDER_DISABLE_MATERIAL_INTERNING')}});(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=120);(out/(name+'.log')).write_text(result.stdout+result.stderr);print(name,result.returncode,flush=True)
 if result.returncode:raise SystemExit(result.returncode)
