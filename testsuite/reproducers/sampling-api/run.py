#!/usr/bin/env python3
"""Sequential Linux sampling API qualification; keep every PASS/FAIL/SKIP."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--artifacts',required=True,type=Path)
p.add_argument('--name',default='api-qualification')
p.add_argument('--profiles',default='wgpu-amd-vulkan,wgpu-nvidia-vulkan,wgpu-amd-gl,bgfx-amd-vulkan,bgfx-nvidia-vulkan,bgfx-amd-gl')
p.add_argument('--modes',default='native,portable')
p.add_argument('--cases',default='api,deep,direct,viewport,procedural')
p.add_argument('--mesa-prefix',type=Path,help='optional external Mesa profile for the CoinGL comparison only')
a=p.parse_args();root=a.artifacts.resolve();out=root/a.name;out.mkdir(parents=True,exist_ok=True)
cases={'api':['CoinRenderSamplingPolicyTest','--gpu'],'selection':['CoinRenderSelectionTest'],
 'publication':['CoinRenderPublicationTest'],'ownership':['CoinRenderRttOwnershipTest'],
 'projective':['CoinRenderTextureSamplingTest','--projective-study'],'sampling':['CoinRenderTextureSamplingTest','--gpu'],
 'advanced':['CoinRenderAdvancedTextureTest','--gpu'],'procedural':['CoinRenderProceduralTextureTest','--gpu'],
 'rtt':['CoinRenderRttProfileTest','--mips-direct'],'deep':['CoinRenderAdvancedTextureTest','--gpu','--sampling-deep-study'],
 'direct':['CoinRenderAdvancedTextureTest','--gpu','--sampling-rtt-study'],
 'viewport':['CoinRenderAdvancedTextureTest','--gpu','--sampling-viewport-study'],
 'large':['CoinWgpuLargeBindingsTest'],'devices':['CoinWgpuMultiDeviceTest'],
 'stress':['CoinWgpuMultiDeviceTest','--stress'],'instancing':['CoinBgfxInstancingTest'],
 'shadow-eight':['CoinRenderShadowEightMapTest'],'shadow-viewport':['CoinRenderShadowReferenceTest','--viewport'],
 'shadow-alpha':['CoinRenderShadowReferenceTest','--alpha-rtt']}
requested=a.cases.split(',');modes=a.modes.split(',')
if any(c not in cases for c in requested) or any(m not in ['native','portable'] for m in modes):p.error('unknown case or mode')
rows=[]
for profile in a.profiles.split(','):
 backend,gpu,api=profile.split('-');build=root/('build-'+backend)
 env=dict(os.environ,DISPLAY=os.environ.get('DISPLAY',':0'),LD_LIBRARY_PATH=str(build/'lib'),
  WGPU_BACKEND='gl' if api=='gl' else 'vulkan',COIN_BGFX_RENDERER='opengl' if api=='gl' else 'vulkan',
  COIN_SAMPLING_STUDY='fetch',COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU='1',COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1')
 for key in ['COIN_MESA_MIXED_FILTER_STUDY','LIBGL_DRIVERS_PATH','AMD_DEBUG','AMD_FORCE_SHADER_USE_ACO','__NV_PRIME_RENDER_OFFLOAD']:env.pop(key,None)
 vendor='nvidia' if gpu=='nvidia' else 'mesa'
 env.update(__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('10_nvidia.json' if gpu=='nvidia' else '50_mesa.json'),
  __GLX_VENDOR_LIBRARY_NAME=vendor,VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('nvidia_icd.json' if gpu=='nvidia' else 'radeon_icd.json'))
 if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
 if a.mesa_prefix and gpu=='amd':
  mesa=a.mesa_prefix.resolve();lib=mesa/'lib/x86_64-linux-gnu'
  env.update(LD_LIBRARY_PATH=str(build/'lib')+':'+str(lib),LIBGL_DRIVERS_PATH=str(lib/'dri'),
   __EGL_VENDOR_LIBRARY_FILENAMES=str(mesa/'share/glvnd/egl_vendor.d/50_mesa.json'),
   VK_DRIVER_FILES=str(mesa/'share/vulkan/icd.d/radeon_icd.x86_64.json'),COIN_MESA_MIXED_FILTER_STUDY='1',MESA_SHADER_CACHE_DISABLE='true',NIR_DEBUG='validate')
 env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
 for case in requested:
  if (case in ['large','devices','stress'] and backend!='wgpu') or (case=='instancing' and backend!='bgfx'):continue
  selected=['both'] if case=='api' else ['native'] if case in ['selection','publication','ownership','large','devices','stress','instancing','shadow-eight','shadow-viewport','shadow-alpha'] else modes
  for mode in selected:
   cmd=[str(build/'bin'/cases[case][0]),*cases[case][1:]]
   if mode=='portable':cmd.append('--portable-sampling')
   log=out/f'{profile}-{mode}-{case}.log'
   with log.open('w') as f:
    try:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=240,cwd=out).returncode
    except subprocess.TimeoutExpired:code=124
   rows.append(dict(profile=profile,policy=mode,case=case,exit=code,command=cmd,log=log.name,
    binary_sha256=hashlib.sha256(Path(cmd[0]).read_bytes()).hexdigest(),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
    environment={k:v for k,v in env.items() if k in ['DISPLAY','XAUTHORITY','LD_LIBRARY_PATH','WGPU_BACKEND','COIN_BGFX_RENDERER','COIN_SAMPLING_STUDY','COIN_RENDER_REQUIRE_GL_REFERENCE','COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU','COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU','VK_DRIVER_FILES','VK_ICD_FILENAMES','__EGL_VENDOR_LIBRARY_FILENAMES','__GLX_VENDOR_LIBRARY_NAME','__NV_PRIME_RENDER_OFFLOAD','LIBGL_DRIVERS_PATH','COIN_MESA_MIXED_FILTER_STUDY','MESA_SHADER_CACHE_DISABLE','NIR_DEBUG']}))
   (out/'summary.json').write_text(json.dumps(rows,indent=2));print(profile,mode,case,code,flush=True)

# Preserve SKIP/FAIL in the ledger and propagate incomplete qualification to callers.
raise SystemExit(0 if all(row["exit"] == 0 for row in rows) else 1)
