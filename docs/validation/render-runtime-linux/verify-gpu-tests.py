import os,subprocess,json,time
from pathlib import Path
out=Path('/tmp/coin-render-runtime-validation');out.mkdir(exist_ok=True)
rows=[]
tasks=[]
for backend in ['bgfx','wgpu']:
 for api in ['vulkan','opengl' if backend=='bgfx' else 'gl']:
  gpu='amd' if backend=='wgpu' and api=='gl' else 'nvidia'
  tasks.append((backend,api,gpu,'depth', ['CoinRenderDepthContractTest','--gpu']))
  tasks.append((backend,api,gpu,'readback', ['CoinBgfxReadbackModesTest'] if backend=='bgfx' else ['CoinRenderAsyncActionTest']))
 tasks += [(backend,'vulkan','nvidia','shadow',['CoinRenderShadowReferenceTest']),
           (backend,'vulkan','nvidia','multi',['CoinRenderMultiTargetTest']),
           (backend,'vulkan','nvidia','multi-direct',['CoinRenderMultiTargetTest'])]
tasks += [('wgpu','vulkan','nvidia','cache',['CoinWgpuCacheTest']),
          ('wgpu','vulkan','nvidia','async-low-level',['CoinRenderAsyncReadbackTest']),
          ('wgpu','vulkan','amd','surface',['CoinRenderSurfaceTest','--require-display']),
          ('bgfx','vulkan','amd','window',['coin_render_window_cone','--frames','3']),
          ('wgpu','vulkan','amd','window',['coin_render_window_cone','--frames','3'])]
for b,api,gpu,name,args in tasks:
 env=os.environ.copy()
 for k in ['__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','WGPU_BACKEND','COIN_BGFX_RENDERER','COIN_RENDER_RTT_GPU_DIRECT']:env.pop(k,None)
 if gpu=='nvidia':env.update(__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia')
 env.update(VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json'),COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
 env['COIN_BGFX_RENDERER' if b=='bgfx' else 'WGPU_BACKEND']=api
 if name=='multi-direct':env['COIN_RENDER_RTT_GPU_DIRECT']='1'
 if name=='shadow':env['COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU' if b=='bgfx' else 'COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU']='1'
 start=time.monotonic()
 p=subprocess.run([f'/tmp/coin-render-first-frame-{b}/bin/'+args[0],*args[1:]],env=env,capture_output=True,text=True,timeout=120)
 stem=f'{b}-{api}-{gpu}-{name}'
 log=p.stdout+p.stderr;(out/(stem+'.log')).write_text(log)
 row=dict(backend=b,api=api,gpu=gpu,test=name,args=args,code=p.returncode,seconds=round(time.monotonic()-start,3))
 rows.append(row);(out/'gpu-tests.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(stem+': '+str(p.returncode),flush=True)
 if p.returncode or 'skipped' in log.lower() or 'RenderAction error:' in log:print(log[-2500:],flush=True)
