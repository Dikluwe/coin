import os,subprocess,json
from pathlib import Path
out=Path('/tmp/coin-render-bgfx-300-validation');out.mkdir(exist_ok=True)
rows=[]
tasks=[]
for b in ['bgfx','wgpu']:
 for test in ['CoinRenderFogTest','CoinRenderSceneTextureTest','CoinRenderTextureTest','CoinRenderDrawStyleTest','CoinRenderCompositionTest','CoinRenderShadowReferenceTest','CoinRenderDepthContractTest','CoinRenderClipPlaneTest','CoinRenderLightingTest','CoinRenderMultitextureTest','CoinRenderTransparencyTest']:
  tasks.append((b,'vulkan','nvidia',test,[]))
 for api in ['vulkan','opengl' if b=='bgfx' else 'gl']:
  tasks.append((b,api,'amd' if b=='wgpu' and api=='gl' else 'nvidia','CoinRenderDepthContractTest',['--gpu']))
for b,api,gpu,test,args in tasks:
 env=os.environ.copy()
 for key in ['__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','COIN_BGFX_RENDERER','WGPU_BACKEND']:env.pop(key,None)
 if gpu=='nvidia':env.update(__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia')
 env.update(VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json'),COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
 env['COIN_BGFX_RENDERER' if b=='bgfx' else 'WGPU_BACKEND']=api
 env['COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU' if b=='bgfx' else 'COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU']='1'
 p=subprocess.run([f'/tmp/coin-render-first-frame-{b}/bin/{test}',*args],env=env,capture_output=True,text=True,timeout=120)
 log=p.stdout+p.stderr;name=f'{b}-{api}-{gpu}-{test}'+('-gpu' if args else '')
 (out/(name+'.log')).write_text(log)
 rows.append(dict(backend=b,api=api,gpu=gpu,test=test,args=args,code=p.returncode));print(name,p.returncode,flush=True)
 if p.returncode or 'skipped' in log.lower():print(log[-2500:],flush=True)
(out/'gpu.json').write_text(json.dumps(rows,indent=2)+'\n')

assert all(x['code']==0 for x in rows), 'GPU test failure'
assert not any('skipped' in p.read_text().lower() for p in out.glob('*.log')), 'Skipped GPU test'
