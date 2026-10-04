import os,json,re,subprocess,hashlib
from pathlib import Path
out=Path('/tmp/coin-render-coingl-reference-results');out.mkdir(exist_ok=True)
variants={
 'coingl-nvidia':('bgfx','gl','nvidia',None),
 'bgfx-vulkan':('bgfx','bgfx','nvidia','vulkan'),
 'bgfx-opengl':('bgfx','bgfx','nvidia','opengl'),
 'wgpu-vulkan':('wgpu','wgpu','nvidia','vulkan'),
 'coingl-amd':('bgfx','gl','amd',None),
 'wgpu-opengl':('wgpu','wgpu','amd','gl'),
}
orders=[list(variants),['wgpu-vulkan','bgfx-opengl','bgfx-vulkan','coingl-nvidia','wgpu-opengl','coingl-amd'],['bgfx-vulkan','coingl-nvidia','wgpu-vulkan','bgfx-opengl','coingl-amd','wgpu-opengl']]
rows=[]
for round_id,order in enumerate(orders,1):
 for variant in order:
  build_backend,backend,gpu,api=variants[variant]
  build=f'/tmp/coin-render-first-frame-{build_backend}'
  env=os.environ.copy()
  for key in ['COIN_RENDER_TRACE_PHASES','COIN_DEBUG_GLGLUE','COIN_GLXGLUE_NO_PBUFFERS','COIN_GLXGLUE_NO_GLX13_PBUFFERS','COIN_GLX_PIXMAP_DIRECT_RENDERING','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','__EGL_VENDOR_LIBRARY_FILENAMES','COIN_BGFX_RENDERER','WGPU_BACKEND','COIN_BGFX_READBACK_PIPELINE_DEPTH','COIN_WGPU_DISABLE_EARLY_OPAQUE_BATCHING','COIN_WGPU_DISABLE_OPAQUE_BATCHING']:env.pop(key,None)
  env['LD_LIBRARY_PATH']=build+'/lib'
  if gpu=='nvidia':env.update(__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json')
  else:env['VK_ICD_FILENAMES']='/usr/share/vulkan/icd.d/radeon_icd.json'
  if backend=='gl':
   env['COIN_GLX_PIXMAP_DIRECT_RENDERING']='1'
   if gpu=='amd':env['COIN_GLXGLUE_NO_PBUFFERS']='1'
  else:env['COIN_BGFX_RENDERER' if backend=='bgfx' else 'WGPU_BACKEND']=api
  stem=f'{variant}-{round_id}'
  cmd=[build+'/bin/coin_render_gl_benchmark','--scene','/tmp/coin-render-city-40000.iv','--backend',backend,'--size','1024','--warmup','30','--frames','120']
  if round_id==1:cmd+=['--image-output',str(out/(stem+'.ppm'))]
  result=subprocess.run(['/usr/bin/time','-f','benchmark_peak_rss_kib=%M',*cmd],env=env,capture_output=True,text=True,timeout=240)
  (out/(stem+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:raise RuntimeError(f'{stem}: {result.returncode}: {result.stderr[-2000:]}')
  label='CoinGL' if backend=='gl' else 'BGFX-Vulkan' if variant=='bgfx-vulkan' else 'BGFX-OpenGL' if backend=='bgfx' else 'WebGPU'
  first=float(re.search(r'^'+label+r'_first_frame_ms=([\d.]+)',result.stdout,re.M)[1])
  steady=re.search(r'^'+label+r' frames=120 median_ms=([\d.]+) p95_ms=([\d.]+)',result.stdout,re.M)
  checksum=re.search(r'^'+('gl_rgba_fnv64' if backend=='gl' else 'rgba_fnv64')+r'=(\S+)',result.stdout,re.M)[1]
  detail=re.search(r'^'+label+r'_first_detail.*result_since_main_ms=([\d.]+)',result.stdout,re.M)
  if backend!='gl':
   adapter=re.search(r'^adapter=(.+?) vendor_id=',result.stdout,re.M)[1]
   assert ('NVIDIA' in adapter if gpu=='nvidia' else 'AMD' in adapter),adapter
  row=dict(variant=variant,reference='coingl-'+gpu,gpu=gpu,round=round_id,first_ms=first,median_ms=float(steady[1]),p95_ms=float(steady[2]),result_since_main_ms=float(detail[1]),checksum=checksum,peak_rss_kib=int(re.search(r'benchmark_peak_rss_kib=(\d+)',result.stderr)[1]))
  if round_id==1:row['image_sha256']=hashlib.sha256((out/(stem+'.ppm')).read_bytes()).hexdigest()
  rows.append(row);(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
  print(f'{stem}: first={first:.2f} ms median={row["median_ms"]:.2f} ms p95={row["p95_ms"]:.2f} ms checksum={checksum}',flush=True)
for variant in variants:
 selected=[r for r in rows if r['variant']==variant]
 assert len(selected)==3 and len(set(r['checksum'] for r in selected))==1,variant
expected={'bgfx-vulkan':'0x6714299260985122','bgfx-opengl':'0x156847cec1d97864','wgpu-vulkan':'0x6714299260985122','wgpu-opengl':'0x3a998bbb78caa846'}
assert all(r['checksum']==expected[r['variant']] for r in rows if r['variant'] in expected)
print('18 runs completed; each renderer preserved its own static checksum',flush=True)
