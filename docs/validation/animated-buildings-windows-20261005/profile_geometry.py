import os, subprocess, shutil
from pathlib import Path
root=Path('H:/Git/coin'); out=root/'build/animated-buildings-windows-20261005'
out.mkdir(exist_ok=True)
baseline=out/'baseline-bin'; baseline.mkdir(exist_ok=True)
for name in ('Coin4.dll','CoinRender4.dll','coin_render_gl_benchmark.exe'):
    dst=baseline/name
    if not dst.exists(): shutil.copy2(root/'build/coin-render-bgfx-msvc/bin'/name,dst)
env=os.environ.copy()
for k in list(env):
    if k.startswith(('COIN_BGFX_', 'COIN_WGPU_', 'COIN_RENDER_DISABLE_')) or k in ('COIN_RENDER_TRACE_PHASES','WGPU_BACKEND'): env.pop(k,None)
env.update(COIN_BGFX_RENDERER='opengl',COIN_BGFX_TRANSPARENCY='auto',COIN_RENDER_TRACE_PHASES='1')
args=[str(baseline/'coin_render_gl_benchmark.exe'),'--backend','bgfx','--scene',str(root/'build/large-scenes/city-40000.iv'),'--size','768','--warmup','2','--frames','4','--animation','geometry','--animated-percent','20','--animation-step','3','--samples-output',str(out/'baseline-trace.csv')]
with (out/'baseline-trace.log').open('wb') as log: r=subprocess.run(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
print('profile exit',r.returncode,flush=True)
