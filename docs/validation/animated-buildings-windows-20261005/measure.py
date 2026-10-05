import json, os, subprocess, re, hashlib
from pathlib import Path
import numpy as np
from PIL import Image

root=Path('H:/Git/coin'); out=root/'build/animated-buildings-windows-20261005'
python_runs=json.loads((out/'runs.json').read_text()) if (out/'runs.json').exists() else []
def run(backend,api,revision,round_number,mode='geometry',frames=30,trace=False,warmup=5):
    label=f'{backend}-{api}-{revision}-{mode}-{round_number}'
    if trace: label += '-trace'
    if warmup==30: label += '-steady'
    if any(r['label']==label and r['exit_code']==0 for r in python_runs): return
    if backend=='coingl': exe=root/'build/coin-render-bgfx-msvc/bin/coin_render_gl_benchmark.exe'
    elif revision=='before': exe=out/('baseline-bin' if backend=='bgfx' else 'wgpu-baseline-bin')/'coin_render_gl_benchmark.exe'
    else: exe=root/f'build/coin-render-{backend}-msvc/bin/coin_render_gl_benchmark.exe'
    env=os.environ.copy()
    for key in list(env):
        if key.startswith(('COIN_BGFX_','COIN_WGPU_','COIN_RENDER_DISABLE_')) or key in ('COIN_RENDER_TRACE_PHASES','WGPU_BACKEND'): env.pop(key,None)
    env['COIN_BGFX_TRANSPARENCY']='auto'
    env['COIN_BGFX_RENDERER']=api if backend=='bgfx' else 'opengl'
    if backend=='wgpu': env['WGPU_BACKEND']=api
    if trace: env['COIN_RENDER_TRACE_PHASES']='1'
    args=[str(exe),'--backend','gl' if backend=='coingl' else backend,'--scene',str(root/'build/large-scenes/city-40000.iv'),'--size','768','--warmup',str(warmup),'--frames',str(frames),'--animation',mode,'--animated-percent','20','--animation-step','3','--samples-output',str(out/(label+'.csv'))]
    if not trace:
        args.extend(['--capture-frames',','.join(map(str, sorted({0,frames//2,frames-1}))), '--capture-prefix',str(out/label)])
    log=out/(label+'.log')
    with log.open('wb') as stream: result=subprocess.run(args,cwd=root,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=240)
    record=dict(label=label,backend=backend,api=api,revision=revision,round=round_number,mode=mode,trace=trace,command=args,exit_code=result.returncode,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),dll_sha256=hashlib.sha256((exe.parent/'CoinRender4.dll').read_bytes()).hexdigest())
    python_runs.append(record); (out/'runs.json').write_text(json.dumps(python_runs,indent=2)+'\n')
    if result.returncode: raise RuntimeError(log.read_text(errors='replace')[-4000:])
    stats=re.search(r'_timing_total frames=\d+ median_ms=([\d.]+)',log.read_text(errors='replace'))
    print(label, 'median_ms='+stats[1],flush=True)

for round_number in range(1,4):
    variants=[('bgfx','opengl','before'),('bgfx','opengl','after'),('coingl','opengl','reference'),('wgpu','gl','before'),('wgpu','gl','after')]
    if round_number%2==0: variants.reverse()
    for backend,api,revision in variants: run(backend,api,revision,round_number)
for backend,api in [('bgfx','vulkan'),('bgfx','d3d12'),('wgpu','vulkan'),('wgpu','dx12')]:
    for revision in ['before','after']: run(backend,api,revision,1)
for round_number in range(1,4):
    for mode in ['static','camera','transforms','materials']:
        for revision in (['before','after'] if round_number%2 else ['after','before']):
            run('bgfx','opengl',revision,round_number,mode,frames=10)
run('bgfx','opengl','after',1,frames=4,trace=True)
for round_number in range(1,4):
    for mode in ['static','camera']:
        for revision in (['before','after'] if round_number%2 else ['after','before']):
            run('bgfx','opengl',revision,round_number,mode,frames=120,warmup=30)
print('measurement campaign complete',flush=True)
