import os,subprocess
base=os.environ.copy();base.update(PATH='/home/linuxbrew/.linuxbrew/bin:/usr/bin:/bin',LD_LIBRARY_PATH='/home/dikluwe/.local/lib/python3.12/site-packages/PySide6/Qt/lib:/tmp/coin-render-first-frame-bgfx/lib:/tmp/freecad-p16-build/Mod/Part:/tmp/freecad-p16-build/Mod/Mesh:/tmp/freecad-p16-build/Mod/Material:/tmp/freecad-p16-build/lib',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',COIN_BGFX_TRACE_GL_ADAPTER='1',COIN_TEST_RETAINED_NODES='1')
import sys,time
kind=sys.argv[1];routes=[('bgfx','vulkan'),('bgfx','opengl'),('wgpu','vulkan')]
for backend,renderer in routes:
 env=base.copy();env['LD_LIBRARY_PATH']=env['LD_LIBRARY_PATH'].replace('first-frame-bgfx','first-frame-'+backend);env['COIN_BGFX_DISABLE_PROGRAM_CACHE']='1'
 case='rtt-window' if kind=='window' else ('retained-rejection' if kind=='rejection' else 'freecad-screen-content')
 if kind=='polygon':env['COIN_TEST_RETAINED_FILTER']='polygon'
 elif kind in ('bspline-curve','bspline-surface'):env.update(COIN_TEST_SPLINE_CONSUMER='1',COIN_TEST_SPLINE_KIND=kind)
 path=f'/tmp/coin-front4-qualified-{kind}-{backend}-{renderer}'
 cmd=['python3','testsuite/qt-quarter/run_isolated.py','--server','xwayland','--weston-prefix','/tmp/coin-p16-weston','--harness','/tmp/coin-front3-qt/QtQuarterRegression','--freecad','/tmp/freecad-p16-build/bin/FreeCAD','--artifacts',path,'--backend',backend,'--renderer',renderer,'--case',case,'--require-hardware','--timeout','180']
 with open(path+'.log','w') as log:code=subprocess.call(cmd,cwd='/tmp/coin-render-first-frame',env=env,stdout=log,stderr=subprocess.STDOUT)
 print(kind,backend,renderer,code,flush=True)
 if code:sys.exit(code)
