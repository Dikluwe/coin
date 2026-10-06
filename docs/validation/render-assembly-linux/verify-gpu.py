import os,subprocess,json
from pathlib import Path
out=Path('/tmp/coin-render-assembly-validation');out.mkdir(exist_ok=True)
rows=[]
for b in ['bgfx','wgpu']:
 env=os.environ.copy();env.update(__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
 env['COIN_BGFX_RENDERER' if b=='bgfx' else 'WGPU_BACKEND']='vulkan'
 env['COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU' if b=='bgfx' else 'COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU']='1'
 for name in ['CoinRenderTextureTest','CoinRenderDrawStyleTest','CoinRenderCompositionTest','CoinRenderShadowReferenceTest','CoinRenderDepthContractTest']:
  p=subprocess.run([f'/tmp/coin-render-first-frame-{b}/bin/{name}'],env=env,capture_output=True,text=True,timeout=120)
  log=p.stdout+p.stderr;(out/f'{b}-{name}.log').write_text(log)
  rows.append(dict(backend=b,test=name,code=p.returncode));print(b,name,p.returncode,flush=True)
  if p.returncode or 'skipped' in log.lower():print(log[-2000:],flush=True)
(out/'gpu.json').write_text(json.dumps(rows,indent=2)+'\n')
