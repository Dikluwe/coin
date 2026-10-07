import os,subprocess,sys
for backend,renderer in [('bgfx','vulkan'),('bgfx','opengl'),('wgpu','vulkan')]:
 env=os.environ.copy();env.update(DISPLAY=':0',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_BGFX_RENDERER=renderer,LD_LIBRARY_PATH=f'/tmp/coin-render-first-frame-{backend}/lib',COIN_BGFX_DISABLE_PROGRAM_CACHE='1',COIN_RENDER_TRACE_PHASES='1',COIN_BGFX_TRACE_GL_ADAPTER='1')
 for executable,args in [('CoinRenderTextureSamplingTest',['--gpu']),('CoinRenderRttProfileTest',[]),('CoinRenderRttProfileTest',['--direct']),('CoinRenderScreenContentTest',['--gpu'])]:
  name=executable+('-direct' if '--direct' in args else '')
  path=f'/tmp/coin-front4-regression-{backend}-{renderer}-{name}.log'
  with open(path,'w') as log:code=subprocess.call([f'/tmp/coin-render-first-frame-{backend}/bin/{executable}',*args],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
  print(backend,renderer,name,code,flush=True)
  if code:sys.exit(code)
