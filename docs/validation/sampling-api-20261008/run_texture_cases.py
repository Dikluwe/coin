from pathlib import Path
import os,subprocess,json,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument('--name',default='api-gpu');p.add_argument('--cases',default='deep,direct,viewport,procedural');p.add_argument('--portable',action='store_true');p.add_argument('--mesa',action='store_true');p.add_argument('--profiles',default='wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-gl,bgfx-amd-vulkan,bgfx-nvidia-vulkan,bgfx-amd-gl');a=p.parse_args()
r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api');out=r/a.name;out.mkdir(exist_ok=True);rows=[]
cases={'deep':['CoinRenderAdvancedTextureTest','--gpu','--sampling-deep-study'],'direct':['CoinRenderAdvancedTextureTest','--gpu','--sampling-rtt-study'],'viewport':['CoinRenderAdvancedTextureTest','--gpu','--sampling-viewport-study'],'procedural':['CoinRenderProceduralTextureTest','--gpu'],'projective':['CoinRenderTextureSamplingTest','--projective-study'],'sampling':['CoinRenderTextureSamplingTest','--gpu'],'advanced':['CoinRenderAdvancedTextureTest','--gpu'],'rtt':['CoinRenderRttProfileTest','--mips-direct'],'selection':['CoinRenderSelectionTest'],'publication':['CoinRenderPublicationTest'],'ownership':['CoinRenderRttOwnershipTest']}
for profile in a.profiles.split(','):
 backend,gpu,api=profile.split('-');build=r/('build-'+backend)
 env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/dikluwe/.Xauthority',LD_LIBRARY_PATH=str(build/'lib'),WGPU_BACKEND='gl' if api=='gl' else 'vulkan',COIN_BGFX_RENDERER='opengl' if api=='gl' else 'vulkan',COIN_SAMPLING_STUDY='fetch',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1')
 for k in ['COIN_MESA_MIXED_FILTER_STUDY','LIBGL_DRIVERS_PATH','AMD_DEBUG','AMD_FORCE_SHADER_USE_ACO']:env.pop(k,None)
 if gpu=='amd':
  env.update(__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json',__GLX_VENDOR_LIBRARY_NAME='mesa',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json');env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
 else:env.update(__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',__NV_PRIME_RENDER_OFFLOAD='1')
 if a.mesa and gpu=='amd':
  mesa=r.parent/'20261008-failure-fixes/mesa-study/install'
  env.update(LD_LIBRARY_PATH=str(build/'lib')+':'+str(mesa/'lib/x86_64-linux-gnu'),LIBGL_DRIVERS_PATH=str(mesa/'lib/x86_64-linux-gnu/dri'),__EGL_VENDOR_LIBRARY_FILENAMES=str(mesa/'share/glvnd/egl_vendor.d/50_mesa.json'),VK_DRIVER_FILES=str(mesa/'share/vulkan/icd.d/radeon_icd.x86_64.json'),COIN_MESA_MIXED_FILTER_STUDY='1',MESA_SHADER_CACHE_DISABLE='true',NIR_DEBUG='validate')
 env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES'];env['COIN_RENDER_REQUIRE_GL_REFERENCE']='1'
 for case in a.cases.split(','):
  cmd=[str(build/'bin'/cases[case][0]),*cases[case][1:]]
  if a.portable:cmd.append('--portable-sampling')
  log=out/(profile+'-'+case+'.log')
  with log.open('w') as f:
   try:c=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=240,cwd=out).returncode
   except subprocess.TimeoutExpired:c=124
  rows.append(dict(profile=profile,policy='portable' if a.portable else 'native',case=case,exit=c,command=cmd,log=log.name,binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),environment={k:v for k,v in env.items() if k in ['DISPLAY','XAUTHORITY','LD_LIBRARY_PATH','WGPU_BACKEND','COIN_BGFX_RENDERER','COIN_SAMPLING_STUDY','VK_DRIVER_FILES','VK_ICD_FILENAMES','__EGL_VENDOR_LIBRARY_FILENAMES','__GLX_VENDOR_LIBRARY_NAME','__NV_PRIME_RENDER_OFFLOAD','LIBGL_DRIVERS_PATH','COIN_MESA_MIXED_FILTER_STUDY','MESA_SHADER_CACHE_DISABLE','NIR_DEBUG','COIN_RENDER_REQUIRE_GL_REFERENCE']}));print(profile,case,c,flush=True)
  (out/'summary.json').write_text(json.dumps(rows,indent=2))
