"""Focused regression gates after all GPU measurements, serial GPU execution."""
import subprocess,os
from pathlib import Path
env=dict(os.environ,DISPLAY=':0',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
for key in list(env):
 if key.startswith(('COIN_BGFX_DISABLE_','COIN_WGPU_DISABLE_')) or key in ('COIN_RENDER_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS','COIN_BGFX_TRACE_GL_ADAPTER','COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE'):env.pop(key)
plans={
 'bgfx':(['CoinBgfxCoreTest','CoinBgfxReadbackModesTest','CoinRenderPublicationTest','CoinRenderMultiTargetTest'],r'^(CoinBgfxCoreTest|CoinBgfxReadbackModes_(vulkan|opengl)|CoinRenderPublicationTest|CoinRenderMultiTarget(Direct|StagedOpenGL|OpenGL)?Test)$'),
 'wgpu':(['CoinWgpuCacheTest','CoinRenderAsyncActionTest','CoinRenderAsyncReadbackTest','CoinRenderPublicationTest','CoinRenderMultiTargetTest'],r'^(CoinWgpuCacheTest|CoinRenderAsyncAction(Direct)?Test|CoinRenderAsyncReadbackTest|CoinRenderPublicationTest|CoinRenderMultiTarget(Direct)?Test)$')}
for backend,(targets,pattern) in plans.items():
 build=Path('/tmp/coin-render-first-frame-'+backend)
 print('BUILD',backend,targets,flush=True)
 subprocess.run(['cmake','--build',str(build),'--parallel','6','--target',*targets],env=env,check=True)
 child=env.copy();child['LD_LIBRARY_PATH']=str(build/'lib')
 if backend=='bgfx':child['COIN_BGFX_RENDERER']='vulkan'
 else:child['WGPU_BACKEND']='vulkan'
 print('GATES',backend,pattern,flush=True)
 subprocess.run(['ctest','--test-dir',str(build),'-R',pattern,'--output-on-failure','-j','1'],env=child,check=True)
