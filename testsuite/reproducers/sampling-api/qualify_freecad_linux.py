#!/usr/bin/env python3
"""Qualify real FreeCAD viewports with explicit policy, isolated profiles and physical GPU evidence."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--artifacts',type=Path,required=True)
p.add_argument('--name',default='freecad-matrix')
p.add_argument('--profiles',default='bgfx-amd-vulkan,bgfx-nvidia-vulkan,bgfx-amd-gl,wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-gl')
p.add_argument('--policies',default='native,portable')
p.add_argument('--scale',type=int,choices=[1,2],default=1)
a=p.parse_args();root=a.artifacts.resolve();repo=Path(__file__).resolve().parents[3];host=root/'freecad-host/build';exe=host/'bin/FreeCAD';out=root/a.name;out.mkdir(parents=True,exist_ok=True);rows=[]
if any(policy not in ['native','portable'] for policy in a.policies.split(',')):p.error('invalid policy')
for profile in a.profiles.split(','):
 backend,gpu,api=profile.split('-');build=root/('build-'+backend)
 for policy in a.policies.split(','):
  dest=out/(profile+'-'+policy);dest.mkdir(exist_ok=True)
  env=dict(os.environ,LD_LIBRARY_PATH='/home/dikluwe/.local/lib/python3.12/site-packages/PySide6/Qt/lib:'+str(build/'lib')+':'+':'.join(str(host/x) for x in ['Mod/Part','Mod/Material','lib']),DRI_PRIME='0',WGPU_BACKEND='gl' if api=='gl' else 'vulkan',COIN_TEST_PORTABLE_SAMPLING='1' if policy=='portable' else '0',COIN_SAMPLING_STUDY='fetch')
  for key in ['__NV_PRIME_RENDER_OFFLOAD','COIN_MESA_MIXED_FILTER_STUDY','LIBGL_DRIVERS_PATH','AMD_DEBUG','AMD_FORCE_SHADER_USE_ACO']:env.pop(key,None)
  env.update(__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('10_nvidia.json' if gpu=='nvidia' else '50_mesa.json'),__GLX_VENDOR_LIBRARY_NAME='nvidia' if gpu=='nvidia' else 'mesa',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json'))
  env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
  if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
  # GL and NVIDIA use the accelerated desktop: this headless session cannot
  # prove DRI3 GL, and its NVIDIA WM exits before launching the host.
  # Profiles/preferences remain owned by run.py; AMD Vulkan gets a private display.
  cmd=['python3',str(repo/'testsuite/qt-quarter'/('run.py' if api=='gl' or gpu=='nvidia' else 'run_isolated.py'))]
  if api!='gl' and gpu!='nvidia':cmd+=['--server','xwayland','--weston-prefix',str(root/'weston/prefix')]
  cmd+=['--artifacts',str(dest),'--harness',str(exe),'--freecad',str(exe),'--case','freecad-sampling-policy','--renderer','opengl' if api=='gl' else 'vulkan','--backend',backend,'--mode','object','--scale',str(a.scale),'--timeout','120','--require-hardware']
  log=dest/'launcher.log'
  with log.open('w') as f:
   try:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180).returncode
   except subprocess.TimeoutExpired:code=124
  results=json.loads((dest/'results.json').read_text()) if (dest/'results.json').exists() else []
  expected=1 if policy=='portable' else 0
  valid=code==0 and len(results)==1 and all(r['status']=='PASS' and r.get('hardware_gpu') and r.get('active_sampling_policy')==expected for r in results)
  rows.append(dict(profile=profile,policy=policy,scale=a.scale,exit=code,valid=valid,command=cmd,results=results,host_sha256=hashlib.sha256((host/'lib/libFreeCADGui.so').read_bytes()).hexdigest(),environment={k:v for k,v in env.items() if k in ['DISPLAY','XAUTHORITY','LD_LIBRARY_PATH','DRI_PRIME','WGPU_BACKEND','COIN_TEST_PORTABLE_SAMPLING','VK_DRIVER_FILES','VK_ICD_FILENAMES','__EGL_VENDOR_LIBRARY_FILENAMES','__GLX_VENDOR_LIBRARY_NAME','__NV_PRIME_RENDER_OFFLOAD','COIN_SAMPLING_STUDY']}))
  (out/'summary.json').write_text(json.dumps(rows,indent=2));print(profile,policy,code,'valid',valid,flush=True)
raise SystemExit(0 if rows and all(x['valid'] for x in rows) else 1)
