#!/usr/bin/env python3
"""Explicit-policy Linux window ABBA measurements; CPU calls and GPU probes stay separate."""
import argparse, csv, hashlib, json, os, re, statistics, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--artifacts',type=Path,required=True)
p.add_argument('--name',default='benchmark-cpu')
p.add_argument('--profiles',default='wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-gl,bgfx-amd-vulkan,bgfx-nvidia-vulkan,bgfx-amd-gl')
p.add_argument('--workloads',default='texture-1,texture-4,texture-8,npot-8,city-40000,city-1000000')
p.add_argument('--kind',choices=['cpu','gpu'],default='cpu')
p.add_argument('--frames',type=int,default=90)
p.add_argument('--warmup',type=int,default=30)
p.add_argument('--resume',action='store_true')
p.add_argument('--large-bgfx-frames',type=int,default=12)
p.add_argument('--large-bgfx-warmup',type=int,default=4)
p.add_argument('--baseline',action='store_true',help='measure the frozen pre-manager API native control')
a=p.parse_args();root=a.artifacts.resolve();out=root/a.name;out.mkdir(parents=True,exist_ok=True)
legacy=root.parent/'20261007/scenes';cities=Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/apk-memory-final')
scenes={f'texture-{u}':legacy/f'texture-{u}-0.5.iv' for u in [1,4,8]}
scenes.update({f'city-{n}':cities/f'city-{n}.iv' for n in [40000,1000000]})
# Reuse exact historical fixtures, freeze a copy and hash every workload.
fixtures=root/'scenes';fixtures.mkdir(exist_ok=True)
for name,source in list(scenes.items()):
 dest=fixtures/(name+'.iv')
 if not dest.exists():dest.write_bytes(source.read_bytes())
 scenes[name]=dest
npot=fixtures/'npot-8.iv'
if not npot.exists():
 n,m=63,65;image=' '.join(hex(0x284060ff if (x//8+y//8)%2 else 0xa0c0e0ff) for y in range(m) for x in range(n))
 layers=' '.join(f'TextureUnit {{ unit {u} }} Texture2 {{ image {n} {m} 4 {image} }} TextureCoordinate2 {{ point [0.03 0.02,96.97 0.02,96.97 96.98,0.03 96.98] }}' for u in range(8))
 npot.write_text('#Inventor V2.1 ascii\nSeparator { LightModel {model BASE_COLOR} Complexity { textureQuality .5 } '+layers+' Coordinate3 {point [-2 -1 0,2 -1 0,2 1 0,-2 1 0]} IndexedFaceSet {coordIndex [0,1,2,-1,0,2,3,-1]} }\n')
scenes['npot-8']=npot
if any(x not in scenes for x in a.workloads.split(',')):p.error('unknown workload')
rows=json.loads((out/'summary.json').read_text()) if a.resume and (out/'summary.json').exists() else []
completed={(r['profile'],r['workload'],r['repeat'],r['policy']) for r in rows if r['valid']}
for profile in a.profiles.split(','):
 backend,gpu,api=profile.split('-');build=root/('build-'+backend)
 env=dict(os.environ,LD_LIBRARY_PATH=str(build/'lib'),COIN_SAMPLING_STUDY='fetch',WGPU_BACKEND='gl' if api=='gl' else 'vulkan',COIN_BGFX_RENDERER='opengl' if api=='gl' else 'vulkan',COIN_RENDER_RENDERER='opengl' if api=='gl' else 'vulkan',DRI_PRIME='0')
 for key in ['COIN_MESA_MIXED_FILTER_STUDY','LIBGL_DRIVERS_PATH','AMD_DEBUG','AMD_FORCE_SHADER_USE_ACO','__NV_PRIME_RENDER_OFFLOAD','COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_SAMPLING_AUDIT','COIN_WGPU_GPU_TIMESTAMPS']:env.pop(key,None)
 env.update(__GLX_VENDOR_LIBRARY_NAME='nvidia' if gpu=='nvidia' else 'mesa',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('10_nvidia.json' if gpu=='nvidia' else '50_mesa.json'),VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json'))
 env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
 if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
 if a.kind=='gpu':env.update(COIN_RENDER_TRACE_PHASES='1',COIN_SAMPLING_AUDIT='1',COIN_WGPU_GPU_TIMESTAMPS='1',COIN_BGFX_TRACE_GL_ADAPTER='1')
 if backend=='bgfx' and api=='gl':
  env['COIN_BGFX_TRACE_GL_ADAPTER']='1'
  # The frozen historical runtime required full phase tracing to emit its GL
  # receipt. Apply the same instrumentation to both sides of this GL A/B.
  if a.baseline:env['COIN_RENDER_TRACE_PHASES']='1'
 for workload in a.workloads.split(','):
  order=['baseline','native','native','baseline'] if a.baseline else ['native','portable','portable','native']
  if a.baseline and profile=='wgpu-amd-gl':continue # historical executable had no GL CLI entry
  for repeat,policy in enumerate(order):
   if (profile,workload,repeat,policy) in completed:continue
   frames=a.large_bgfx_frames if backend=='bgfx' and workload=='city-1000000' else a.frames
   warmup=a.large_bgfx_warmup if backend=='bgfx' and workload=='city-1000000' else a.warmup
   selected=root/'baseline'/backend if policy=='baseline' else build
   env['LD_LIBRARY_PATH']=str(selected/'lib')
   cmd=[str(selected/'bin/coin_render_window_benchmark'),'--backend',backend+('-opengl' if api=='gl' else '-vulkan'),'--scene',str(scenes[workload]),'--width','1280','--height','720','--frames',str(frames),'--warmup',str(warmup)]
   if policy!='baseline':cmd+=['--sampling-policy',policy]
   if profile=='wgpu-amd-gl':cmd+=['--allow-vsync']
   if workload.startswith('city'):cmd+=['--animation','static']
   key=f'{profile}-{workload}-{repeat}-{policy}';log=out/(key+'.log');samples=out/(key+'.csv');cmd+=['--samples-output',str(samples)]
   with log.open('w') as f:
    try:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=240).returncode
    except subprocess.TimeoutExpired:code=124
   output=log.read_text(errors='replace');report=next((line for line in output.splitlines() if line.startswith('window_benchmark ')),None)
   valid=code==0 and report is not None and 'readback=none' in report and not any(s in report.lower() for s in ['llvmpipe','lavapipe','softpipe'])
   values=[]
   if samples.exists():
    with samples.open() as f:
     for row in csv.DictReader(f):
      if row['warmup']=='0':values.append(float(row['render_present_ms']))
   # Require the requested physical GPU from the actual renderer/context.
   # BGFX GL capability vendor/device can be zero; use its GL driver receipt.
   contexts=re.findall(r'^COIN_RENDER_PHASE bgfx_gl_adapter vendor=(.*?) renderer=(.*?) version=(.*)$',output,re.MULTILINE)
   if backend=='bgfx' and api=='gl':
    hardware=bool(contexts) and all('amd radeon' in (v+' '+n).lower() and bool(ver) and not any(w in (v+' '+n).lower() for w in ['llvmpipe','lavapipe','softpipe','software','virgl','cpu']) for v,n,ver in contexts)
   else:
    expected_vendor='0x1002' if gpu=='amd' else '0x10de'
    hardware=report is not None and 'vendor_id='+expected_vendor+' ' in report and ('AMD' if gpu=='amd' else 'NVIDIA') in report
   valid=valid and hardware
   if len(values)!=frames:valid=False
   gpu_values=[float(x) for x in re.findall(r'(?:COIN_SAMPLING_AUDIT_GPU status=ok render_ms=|gpu_frame_ms=)([0-9.]+)',output)]
   if a.kind=='gpu' and len(gpu_values[warmup:])!=frames:valid=False
   row=dict(cpu_instrumented=env.get('COIN_RENDER_TRACE_PHASES')=='1',hardware_gpu=hardware,gl_contexts=contexts,profile=profile,workload=workload,policy=policy,repeat=repeat,exit=code,valid=valid,command=cmd,log=log.name,samples=samples.name,report=report,binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),scene_sha256=hashlib.sha256(scenes[workload].read_bytes()).hexdigest(),environment={k:v for k,v in env.items() if k in ['DISPLAY','XAUTHORITY','LD_LIBRARY_PATH','WGPU_BACKEND','COIN_BGFX_RENDERER','COIN_RENDER_RENDERER','DRI_PRIME','__GLX_VENDOR_LIBRARY_NAME','__EGL_VENDOR_LIBRARY_FILENAMES','VK_DRIVER_FILES','VK_ICD_FILENAMES','__NV_PRIME_RENDER_OFFLOAD','COIN_SAMPLING_STUDY','COIN_RENDER_TRACE_PHASES','COIN_SAMPLING_AUDIT','COIN_WGPU_GPU_TIMESTAMPS','COIN_BGFX_TRACE_GL_ADAPTER']},cpu_median_ms=statistics.median(values) if values else None,gpu_values_ms=gpu_values[warmup:],gpu_median_ms=statistics.median(gpu_values[warmup:]) if len(gpu_values)>warmup else None)
   rows.append(row);(out/'summary.json').write_text(json.dumps(rows,indent=2));print(key,code,'valid',valid,flush=True)
raise SystemExit(0 if rows and all(x['valid'] for x in rows) else 1)
