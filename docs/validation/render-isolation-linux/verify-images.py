import json,os,re,subprocess
from pathlib import Path
out=Path('/tmp/coin-render-isolation-validation');out.mkdir(exist_ok=True)
rows=[]
for variant,b,gpu,api,expected in [
 ('bgfx-vulkan','bgfx','nvidia','vulkan','0x6714299260985122'),
 ('bgfx-opengl','bgfx','nvidia','opengl','0x156847cec1d97864'),
 ('wgpu-vulkan','wgpu','nvidia','vulkan','0x6714299260985122'),
 ('wgpu-opengl','wgpu','amd','gl','0x3a998bbb78caa846')]:
 env=os.environ.copy()
 for k in ['__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','COIN_BGFX_RENDERER','WGPU_BACKEND','COIN_RENDER_TRACE_PHASES']:env.pop(k,None)
 if gpu=='nvidia':env.update(__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia')
 env['VK_ICD_FILENAMES']='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json')
 env['COIN_BGFX_RENDERER' if b=='bgfx' else 'WGPU_BACKEND']=api
 p=subprocess.run([f'/tmp/coin-render-first-frame-{b}/bin/coin_render_gl_benchmark','--scene','/tmp/coin-render-city-40000.iv','--backend',b,'--size','1024','--warmup','2','--frames','3'],env=env,capture_output=True,text=True,timeout=120)
 (out/(variant+'.log')).write_text(p.stdout+p.stderr)
 m=re.search('rgba_fnv64=(\\S+)',p.stdout)
 row=dict(variant=variant,code=p.returncode,checksum=m[1] if m else '',expected=expected)
 rows.append(row);print(row,flush=True)
 if p.returncode or row['checksum']!=expected:raise RuntimeError(p.stderr[-2000:]+str(row))
(out/'images.json').write_text(json.dumps(rows,indent=2)+'\n')
