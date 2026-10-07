import os, subprocess, pathlib, json
root=pathlib.Path('/tmp/coin-hardware-20261007'); results=[]
for gpu in ('amd','nvidia'):
 for backend,api in (('bgfx','vulkan'),('bgfx','opengl'),('wgpu','vulkan')):
  build='/tmp/coin-render-first-frame-'+backend
  key=f'{gpu}-{backend}-{api}'; out=root/key; out.mkdir(exist_ok=True)
  env=dict(os.environ,DISPLAY=':0',COIN_BGFX_RENDERER=api,COIN_SHADOW_GPU_VENDOR_PATTERN='AMD' if gpu=='amd' else 'NVIDIA',LD_LIBRARY_PATH=build+'/lib',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('radeon' if gpu=='amd' else 'nvidia')+'_icd.json',__GLX_VENDOR_LIBRARY_NAME='mesa' if gpu=='amd' else 'nvidia',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('50_mesa' if gpu=='amd' else '10_nvidia')+'.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1' if gpu=='amd' else '0')
  env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
  if gpu=='nvidia': env['__NV_PRIME_RENDER_OFFLOAD']='1'
  else: env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
  cmd=['bash','.github/scripts/qualify-coin-render-shadows-linux.sh',build,backend,str(out)]
  with (out/'runner.log').open('w') as f: p=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT)
  results.append({'cell':key,'command':cmd,'exit':p.returncode,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','__','VK_','LD_LIBRARY','DISPLAY'))}})
  print(key,p.returncode,flush=True)
  env.update(COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1',COIN_RENDER_REQUIRE_GL_REFERENCE='1')
  for test in ('CoinBumpGLXTest','CoinRenderCameraReuseReferenceTest'):
   with (out/(test+'.log')).open('w') as f: p=subprocess.run([build+'/bin/'+test],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=150)
   results.append({'cell':key,'test':test,'exit':p.returncode}); print(key,test,p.returncode,flush=True)
  (root/'results.json').write_text(json.dumps(results,indent=2)+'\n')
