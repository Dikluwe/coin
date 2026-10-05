import hashlib, json, os, platform, subprocess
from pathlib import Path
root=Path('H:/Git/coin'); out=root/'build/animated-buildings-windows-20261005'
env=os.environ.copy()
for key in list(env):
    if key.startswith(('COIN_BGFX_','COIN_WGPU_','COIN_RENDER_DISABLE_')) or key in ('COIN_RENDER_TRACE_PHASES','WGPU_BACKEND'): env.pop(key,None)
env['COIN_DEBUG_GLGLUE']='1'
args=[str(root/'build/coin-render-bgfx-msvc/bin/coin_render_gl_benchmark.exe'),'--backend','gl','--scene',str(root/'build/large-scenes/city-40000.iv'),'--size','768','--warmup','0','--frames','1']
with (out/'coingl-gpu-diagnostic.log').open('wb') as log:
    result=subprocess.run(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
assert result.returncode==0
gpu=subprocess.check_output(['nvidia-smi','--query-gpu=name,driver_version,pci.bus_id','--format=csv,noheader'],text=True).strip()
bins=[]
for name,folder in [('bgfx-before',out/'baseline-bin'),('wgpu-before',out/'wgpu-baseline-bin'),('bgfx-after',root/'build/coin-render-bgfx-msvc/bin'),('wgpu-after',root/'build/coin-render-wgpu-msvc/bin')]:
    bins.append(dict(name=name,files={str(folder/file):hashlib.sha256((folder/file).read_bytes()).hexdigest() for file in ['Coin4.dll','CoinRender4.dll','coin_render_gl_benchmark.exe']}))
(out/'hardware-and-binaries.json').write_text(json.dumps(dict(platform=platform.platform(),gpu=gpu,coingl_diagnostic_command=args,binaries=bins),indent=2)+'\n')
print(gpu,flush=True)
