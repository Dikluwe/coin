import os,subprocess,sys,json
backend,renderer=sys.argv[1:3]
mechanism=sys.argv[3] if len(sys.argv)>3 else "fbo"
env=os.environ.copy();env.update(DISPLAY=':0',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_BGFX_RENDERER=renderer,LD_LIBRARY_PATH=f'/tmp/coin-render-first-frame-{backend}/lib',COIN_BGFX_DISABLE_PROGRAM_CACHE='1',COIN_RENDER_TRACE_PHASES='1',COIN_BGFX_TRACE_GL_ADAPTER='1')
env['COIN_DONT_USE_FBO']='1' if mechanism=='pbuffer' else '0'
path=f'/tmp/coin-front4-{backend}-{renderer}-mips-{mechanism}.log'
open(path+'.environment.json','w').write(json.dumps({key:env[key] for key in ('DISPLAY','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME','VK_ICD_FILENAMES','COIN_GLX_PIXMAP_DIRECT_RENDERING','COIN_RENDER_REQUIRE_GL_REFERENCE','COIN_BGFX_RENDERER','LD_LIBRARY_PATH','COIN_BGFX_DISABLE_PROGRAM_CACHE','COIN_DONT_USE_FBO')},indent=2)+'\n')
with open(path,'w') as log:code=subprocess.call([f'/tmp/coin-render-first-frame-{backend}/bin/CoinRenderRttProfileTest','--mips'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
print(path,code,flush=True);sys.exit(code)
