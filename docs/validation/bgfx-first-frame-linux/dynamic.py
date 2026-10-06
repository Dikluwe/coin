import os,subprocess,re,json
from pathlib import Path
import numpy as np
from PIL import Image
out=Path('/tmp/coin-render-bgfx-300-validation');out.mkdir(exist_ok=True)
rows=[]
for backend,api in [('bgfx','vulkan'),('bgfx','opengl'),('wgpu','vulkan')]:
 for mode in ['dynamic','material-dynamic']:
  pair=[]
  for version in ['before','after']:
   build='/tmp/coin-render-bgfx-300-baseline/'+backend if version=='before' else '/tmp/coin-render-first-frame-'+backend
   env=os.environ.copy()
   for key in ['COIN_RENDER_TRACE_PHASES','COIN_BGFX_DISABLE_INSTANCING','COIN_BGFX_DISABLE_SMALL_RUNTIME_RESERVATIONS','COIN_BGFX_DISABLE_DRAW_BATCHING','COIN_BGFX_DISABLE_DRAW_GROUPING']:env.pop(key,None)
   env.update(LD_LIBRARY_PATH=build+'/lib',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json')
   env['COIN_BGFX_RENDERER' if backend=='bgfx' else 'WGPU_BACKEND']=api
   stem=backend+'-'+api+'-'+mode+'-'+version
   args=[build+'/bin/coin_render_gl_benchmark','--backend',backend,'--scene','/tmp/coin-render-city-10000.iv','--size','1024','--warmup','1','--frames','2','--'+mode,'--image-output',str(out/(stem+'.ppm'))]
   p=subprocess.run(args,env=env,capture_output=True,text=True,timeout=120)
   (out/(stem+'.log')).write_text(p.stdout+p.stderr)
   if p.returncode:raise RuntimeError(stem+':'+p.stderr[-2000:])
   checksum=re.search(r'rgba_fnv64=(\S+)',p.stdout)[1]
   pair.append((stem,checksum,np.asarray(Image.open(out/(stem+'.ppm')),dtype=np.int16)))
  diff=np.abs(pair[0][2]-pair[1][2])
  row=dict(backend=backend,api=api,mode=mode,before=pair[0][1],after=pair[1][1],rgb_mae=float(diff.mean()),max_channel_error=int(diff.max()),pixels_over3=int(np.any(diff>3,axis=2).sum()))
  rows.append(row);(out/'dynamic.json').write_text(json.dumps(rows,indent=2)+'\n');print(row,flush=True)
  assert row['before']==row['after'],row
print('Dynamic camera/material checksums matched baseline in both BGFX APIs and wgpu Vulkan',flush=True)
