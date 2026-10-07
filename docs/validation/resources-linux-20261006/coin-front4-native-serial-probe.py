import os,subprocess
base=os.environ.copy();base.update(PATH='/home/linuxbrew/.linuxbrew/bin:/usr/bin:/bin',LD_LIBRARY_PATH='/home/dikluwe/.local/lib/python3.12/site-packages/PySide6/Qt/lib:/tmp/coin-render-first-frame-bgfx/lib:/tmp/freecad-p16-build/Mod/Part:/tmp/freecad-p16-build/Mod/Mesh:/tmp/freecad-p16-build/Mod/Material:/tmp/freecad-p16-build/lib',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',COIN_BGFX_TRACE_GL_ADAPTER='1',COIN_TEST_RETAINED_NODES='1')
base['LD_LIBRARY_PATH']=base['LD_LIBRARY_PATH'].replace('first-frame-bgfx','first-frame-wgpu')
base['COIN_RENDER_TRACE_PHASES']='1'
path='/tmp/coin-front4-surface-serial'
cmd=['python3','testsuite/qt-quarter/run_isolated.py','--server','xwayland','--weston-prefix','/tmp/coin-p16-weston','--artifacts',path,'--exec','--','/tmp/coin-render-first-frame-wgpu/bin/CoinRenderSurfaceTest','--camera-only','--require-vulkan']
with open(path+'.log','w') as log:code=subprocess.call(cmd,cwd='/tmp/coin-render-first-frame',env=base,stdout=log,stderr=subprocess.STDOUT,timeout=180)
print(path,code,flush=True);raise SystemExit(code)
