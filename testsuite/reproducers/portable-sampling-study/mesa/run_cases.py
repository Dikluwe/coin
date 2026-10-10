from pathlib import Path
import os,subprocess,json,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument('--artifacts',type=Path,required=True);p.add_argument('--mesa-prefix',type=Path);p.add_argument('--gl-compiler',choices=['aco','llvm-vs-aco-ps'],default='aco');p.add_argument('--name',required=True);p.add_argument('--profiles',default='wgpu-amd-vulkan,wgpu-amd-gl,bgfx-amd-vulkan,bgfx-amd-gl');p.add_argument('--modes',default='native,fine_uniform');p.add_argument('--disabled',action='store_true');p.add_argument('--cases',default='projective');a=p.parse_args()
root=a.artifacts.resolve();mesa=(a.mesa_prefix or root/'mesa-study/install').resolve();out=root/a.name;out.mkdir(parents=True,exist_ok=True);rows=[]
cases={'projective':['CoinRenderTextureSamplingTest','--projective-study'],'sampling':['CoinRenderTextureSamplingTest','--gpu'],
       'deep':['CoinRenderAdvancedTextureTest','--gpu','--sampling-deep-study'],'direct':['CoinRenderAdvancedTextureTest','--gpu','--sampling-rtt-study'],
       'viewport':['CoinRenderAdvancedTextureTest','--gpu','--sampling-viewport-study'],'advanced':['CoinRenderAdvancedTextureTest','--gpu'],
       'procedural':['CoinRenderProceduralTextureTest','--gpu'],'rtt':['CoinRenderRttProfileTest','--mips-direct'],
       'geometry':['CoinRenderGeometryViewportTest','--gpu'],'curved':['CoinRenderGeometryViewportTest','--study-curved-styles'],
       'styles':['CoinRenderDrawStyleTest'],'portable-styles':['CoinRenderDrawStyleTest'],'shadow-eight':['CoinRenderShadowEightMapTest'],'shadow-viewport':['CoinRenderShadowReferenceTest','--viewport'],
       'shadow-alpha':['CoinRenderShadowReferenceTest','--alpha-rtt'],'large':['CoinWgpuLargeBindingsTest'],'devices':['CoinWgpuMultiDeviceTest'],
       'stress':['CoinWgpuMultiDeviceTest','--stress'],'instancing':['CoinBgfxInstancingTest']}
profiles='wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-gl,bgfx-amd-vulkan,bgfx-nvidia-vulkan,bgfx-amd-gl' if a.profiles=='all6' else a.profiles
for profile in profiles.split(','):
 backend,gpu,api=profile.split('-');build=root/('build-'+backend)
 env=dict(os.environ,DISPLAY=':0',XAUTHORITY='/home/dikluwe/.Xauthority',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1',COIN_RENDER_REQUIRE_GL_REFERENCE='1',
     WGPU_BACKEND='gl' if api=='gl' else 'vulkan',COIN_BGFX_RENDERER='opengl' if api=='gl' else 'vulkan',
     COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU='1',COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU='1',MESA_SHADER_CACHE_DISABLE='true',NIR_DEBUG='validate')
 env.pop('AMD_DEBUG',None);env.pop('AMD_FORCE_SHADER_USE_ACO',None)
 if gpu=='amd':
  if a.gl_compiler=='llvm-vs-aco-ps':env.update(AMD_DEBUG='llvm',AMD_FORCE_SHADER_USE_ACO='ps')
  env.update(LD_LIBRARY_PATH=str(build/'lib')+':'+str(mesa/'lib/x86_64-linux-gnu'),LIBGL_DRIVERS_PATH=str(mesa/'lib/x86_64-linux-gnu/dri'),
     __EGL_VENDOR_LIBRARY_FILENAMES=str(mesa/'share/glvnd/egl_vendor.d/50_mesa.json'),__GLX_VENDOR_LIBRARY_NAME='mesa',
     VK_DRIVER_FILES=str(mesa/'share/vulkan/icd.d/radeon_icd.x86_64.json'))
  env.pop('__NV_PRIME_RENDER_OFFLOAD',None);env.pop('COIN_MESA_MIXED_FILTER_STUDY',None)
  if not a.disabled:env['COIN_MESA_MIXED_FILTER_STUDY']='1'
 else:
  env.update(LD_LIBRARY_PATH=str(build/'lib'),__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',
     __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json')
  env.pop('COIN_MESA_MIXED_FILTER_STUDY',None)
 env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
 for mode in a.modes.split(','):
  env['COIN_SAMPLING_STUDY']=mode
  for case in a.cases.split(','):
   if (case in ['large','devices','stress'] and backend!='wgpu') or (case=='instancing' and backend!='bgfx'):continue
   cmd=[str(build/'bin'/cases[case][0]),*cases[case][1:]];log=out/f'{profile}-{mode}-{case}.log'
   caseenv=env.copy()
   if case=='portable-styles':caseenv.pop('COIN_RENDER_REQUIRE_GL_REFERENCE',None)
   with log.open('w') as f:
    try:code=subprocess.run(cmd,env=caseenv,stdout=f,stderr=subprocess.STDOUT,timeout=240,cwd=out).returncode
    except subprocess.TimeoutExpired:code=124
   text=log.read_text(errors='replace');row=dict(profile=profile,mode=mode,case=case,command=cmd,exit=code,log=log.name,
     environment={k:v for k,v in caseenv.items() if k in ['DISPLAY','XAUTHORITY','WGPU_BACKEND','COIN_BGFX_RENDERER','COIN_SAMPLING_STUDY','LD_LIBRARY_PATH','LIBGL_DRIVERS_PATH','__EGL_VENDOR_LIBRARY_FILENAMES','__GLX_VENDOR_LIBRARY_NAME','__NV_PRIME_RENDER_OFFLOAD','VK_DRIVER_FILES','COIN_MESA_MIXED_FILTER_STUDY','MESA_SHADER_CACHE_DISABLE','NIR_DEBUG','AMD_DEBUG','AMD_FORCE_SHADER_USE_ACO','VK_ICD_FILENAMES','COIN_RENDER_REQUIRE_GL_REFERENCE','COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU','COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU']},
     binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
     adapter=next((s for s in text.splitlines() if s.startswith('adapter=')),None))
   rows.append(row);(out/'summary.json').write_text(json.dumps(rows,indent=2));print(profile,mode,case,code,flush=True)
