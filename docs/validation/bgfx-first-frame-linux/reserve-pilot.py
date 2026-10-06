import os,subprocess,re,json
from pathlib import Path
out=Path('/tmp/coin-render-bgfx-300-reserve-pilot');out.mkdir(exist_ok=True)
rows=[]
for api in ['vulkan','opengl']:
 for version in ['before','after','after-runtime-default','after-no-reserve']:
  env=os.environ.copy()
  for x in ['COIN_BGFX_DISABLE_SOLID_PROGRAM','COIN_BGFX_DISABLE_SHARED_RANGE_LOWERING','COIN_BGFX_DISABLE_COMPACT_VERTICES','COIN_RENDER_DISABLE_CAPTURE_RESERVE','COIN_BGFX_DISABLE_SMALL_RUNTIME_RESERVATIONS']:env.pop(x,None)
  build='/tmp/coin-render-bgfx-300-baseline/bgfx' if version=='before' else '/tmp/coin-render-first-frame-bgfx'
  env.update(LD_LIBRARY_PATH=build+'/lib',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',COIN_BGFX_RENDERER=api,COIN_RENDER_TRACE_PHASES='1')
  if version=='after-runtime-default':env['COIN_BGFX_DISABLE_SMALL_RUNTIME_RESERVATIONS']='1'
  if version=='after-no-reserve':env['COIN_RENDER_DISABLE_CAPTURE_RESERVE']='1'
  stem=api+'-'+version
  r=subprocess.run([build+'/bin/coin_render_gl_benchmark','--backend','bgfx','--scene','/tmp/coin-render-city-40000.iv','--size','1024','--warmup','1','--frames','1','--image-output',str(out/(stem+'.ppm'))],env=env,capture_output=True,text=True,timeout=120)
  (out/(stem+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(stem+': '+r.stderr[-2000:])
  row={'api':api,'version':version,'first_ms':float(re.search(r'_first_frame_ms=([\d.]+)',r.stdout)[1]),'checksum':re.search(r'rgba_fnv64=(\S+)',r.stdout)[1]}
  rows.append(row);(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
  for line in (r.stdout+r.stderr).splitlines():
   if any(x in line for x in ['_first_frame_ms=','COIN_RENDER_PHASE action traversal','COIN_RENDER_PHASE target validation','COIN_RENDER_PHASE bgfx_geometry','COIN_RENDER_PHASE bgfx_base_program','COIN_RENDER_PHASE bgfx lower_ms']): print(stem,line,flush=True)
