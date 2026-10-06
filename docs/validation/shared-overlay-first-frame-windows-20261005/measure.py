"""Serial Windows offscreen campaign. Requires successful Release builds/tests."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('H:/Git/coin')
OUT = ROOT / 'build/shared-overlay-first-frame-windows-20261005'
SOURCE = ROOT / 'build/coin-render-source'
SCENE = ROOT / 'build/large-scenes/city-40000.iv'
VARIANTS = [('coingl','bgfx','gl',None), ('bgfx-opengl','bgfx','bgfx','opengl'),
    ('bgfx-vulkan','bgfx','bgfx','vulkan'), ('bgfx-d3d12','bgfx','bgfx','d3d12'),
    ('wgpu-opengl','wgpu','wgpu','gl'), ('wgpu-vulkan','wgpu','wgpu','vulkan'),
    ('wgpu-d3d12','wgpu','wgpu','dx12')]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def environment(backend, api, trace=False):
    env=os.environ.copy()
    for key in list(env):
        if key.startswith(('COIN_BGFX_', 'COIN_WGPU_', 'COIN_RENDER_DISABLE_')) or key in (
            'WGPU_BACKEND', 'COIN_RENDER_TRACE_PHASES', 'COIN_DEBUG_GLGLUE',
            'VK_ICD_FILENAMES', 'VK_DRIVER_FILES', 'COIN_RENDER_RTT_GPU_DIRECT'):
            env.pop(key, None)
    env['COIN_BGFX_TRANSPARENCY']='auto'
    if api: env['COIN_BGFX_RENDERER' if backend=='bgfx' else 'WGPU_BACKEND']=api
    if trace: env['COIN_RENDER_TRACE_PHASES']='1'
    return env

runs=json.loads((OUT/'runs.json').read_text(encoding='utf-8')) if (OUT/'runs.json').exists() else []

def run(name, build_backend, renderer, api, revision, kind, sample, extra, trace=False, scene=SCENE):
    directory = OUT / ('baseline-' + build_backend) if revision=='before' else ROOT / ('build/coin-render-' + build_backend + '-msvc/bin')
    exe=directory / 'coin_render_gl_benchmark.exe'
    tag=f'{kind}-{name}-{revision}-{sample}'
    image=OUT / (tag+'.ppm')
    arguments=[str(exe),'--backend',renderer,'--scene',str(scene),'--size','1024',*extra,
        '--image-output',str(image)]
    for previous in runs:
        if previous['command']==arguments and previous['exit_code']==0:
            assert digest(image)==previous['image_sha256']
            print(tag,'already complete',flush=True)
            return
    env=environment(build_backend,api,trace)
    if kind=='gl-diagnostic': env['COIN_DEBUG_GLGLUE']='1'
    log=OUT/(tag+'.log')
    with log.open('wb') as stream:
        completed=subprocess.run(arguments,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=300)
    result=dict(variant=name,revision=revision,kind=kind,sample=sample,command=arguments,
        log=log.name,image=image.name,exit_code=completed.returncode,tracing=trace,
        scene=str(scene),scene_sha256=digest(scene))
    if image.exists(): result['image_sha256']=digest(image)
    runs.append(result)
    (OUT/'runs.json').write_text(json.dumps(runs,indent=2)+'\n',encoding='utf-8')
    print(tag,'exit='+str(completed.returncode),flush=True)
    if completed.returncode:
        print(log.read_text(encoding='utf-8',errors='replace')[-3000:],flush=True)
        raise RuntimeError('Benchmark failed: '+tag)

run(*VARIANTS[0],'after','gl-diagnostic',1,['--warmup','1','--frames','1'])
for sample in range(1,4):
    order=VARIANTS if sample%2 else list(reversed(VARIANTS))
    for variant in order:
        for revision in ('before','after'):
            run(*variant,revision,'static',sample,['--warmup','30','--frames','120'])
for variant in VARIANTS[1:]:
    run(*variant,'after','trace',1,['--warmup','1','--frames','3'],trace=True)
for mode in ('camera','transforms','materials','geometry'):
    for variant in VARIANTS:
        tag='animation-'+variant[0]+'-'+mode
        run(*variant,'after','animation',mode,
            ['--warmup','2','--frames','5','--animation',mode,'--animated-percent','10',
             '--animation-step','120','--capture-frames','0,1,2,3,4',
             '--capture-prefix',str(OUT/tag),'--samples-output',str(OUT/(tag+'.csv'))])
palette=OUT/'city-materials-40001.iv'
for variant in VARIANTS:
    tag='palette-'+variant[0]
    run(*variant,'after','palette','materials',
        ['--warmup','2','--frames','5','--animation','materials','--animated-percent','10',
         '--animation-step','120','--capture-frames','0,1,2,3,4',
         '--capture-prefix',str(OUT/tag),'--samples-output',str(OUT/(tag+'.csv'))],scene=palette)
print('Campaign complete:',len(runs),'processes',flush=True)
