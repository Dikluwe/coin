import os,subprocess,re,json
from pathlib import Path
out=Path('/tmp/coin-render-transform-validation');rows=[]
prior=json.loads(Path('/tmp/coin-render-first-frame-results/controls.json').read_text())
for b in ['bgfx','wgpu']:
 env=os.environ.copy();env.update(__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json')
 env['COIN_BGFX_RENDERER' if b=='bgfx' else 'WGPU_BACKEND']='vulkan'
 for mode in ['dynamic','material-dynamic']:
  expected=next(x['after'] for x in prior if x['backend']==b and x.get('mode')==mode)
  args=[f'/tmp/coin-render-first-frame-{b}/bin/coin_render_gl_benchmark','--backend',b,'--scene','/tmp/coin-render-city-10000.iv','--size','1024','--warmup','1','--frames','2','--'+mode]
  p=subprocess.run(args,env=env,capture_output=True,text=True,timeout=120)
  (out/f'{b}-{mode}.log').write_text(p.stdout+p.stderr)
  match=re.search(r'rgba_fnv64=(\S+)',p.stdout)
  actual=match[1] if match else ''
  rows.append(dict(backend=b,mode=mode,code=p.returncode,checksum=actual,expected=expected));print(rows[-1],flush=True)
  if p.returncode or actual!=expected:raise RuntimeError(p.stderr[-1000:]+str(rows[-1]))
(out/'dynamic.json').write_text(json.dumps(rows,indent=2)+'\n')
