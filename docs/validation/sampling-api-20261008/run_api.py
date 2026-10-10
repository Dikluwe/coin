from pathlib import Path
import os,subprocess,json,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument('--name',default='api-gpu');p.add_argument('--program',default='CoinRenderSamplingPolicyTest');p.add_argument('--arguments',default='--gpu');p.add_argument('--profiles',default='wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-gl,bgfx-amd-vulkan,bgfx-nvidia-vulkan,bgfx-amd-gl');a=p.parse_args()
r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api');out=r/a.name;out.mkdir(exist_ok=True);rows=[]
for profile in a.profiles.split(','):
 backend,gpu,api=profile.split('-');build=r/('build-'+backend)
 env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/dikluwe/.Xauthority',LD_LIBRARY_PATH=str(build/'lib'),WGPU_BACKEND='gl' if api=='gl' else 'vulkan',COIN_BGFX_RENDERER='opengl' if api=='gl' else 'vulkan',COIN_SAMPLING_STUDY='fetch',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1')
 for k in ['COIN_MESA_MIXED_FILTER_STUDY','LIBGL_DRIVERS_PATH','AMD_DEBUG','AMD_FORCE_SHADER_USE_ACO']:env.pop(k,None)
 if gpu=='amd':
  env.update(__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json',__GLX_VENDOR_LIBRARY_NAME='mesa',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json');env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
 else:env.update(__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',__NV_PRIME_RENDER_OFFLOAD='1')
 env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES'];cmd=[str(build/'bin'/a.program),*a.arguments.split()];log=out/(profile+'.log')
 with log.open('w') as f:
  try:c=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=240,cwd=out).returncode
  except subprocess.TimeoutExpired:c=124
 rows.append(dict(profile=profile,exit=c,command=cmd,log=log.name,binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),environment={k:v for k,v in env.items() if k in ['DISPLAY','XAUTHORITY','LD_LIBRARY_PATH','WGPU_BACKEND','COIN_BGFX_RENDERER','COIN_SAMPLING_STUDY','VK_DRIVER_FILES','VK_ICD_FILENAMES','__EGL_VENDOR_LIBRARY_FILENAMES','__GLX_VENDOR_LIBRARY_NAME','__NV_PRIME_RENDER_OFFLOAD']}));print(profile,c,flush=True)
 (out/'summary.json').write_text(json.dumps(rows,indent=2))
