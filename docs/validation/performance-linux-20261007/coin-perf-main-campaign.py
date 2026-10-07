from pathlib import Path
import os,subprocess,json,sys
repo=Path('/tmp/coin-render-first-frame');root=Path('/tmp/coin-perf-main');root.mkdir(exist_ok=True)
env=dict(os.environ,DISPLAY=':0',__NV_PRIME_RENDER_OFFLOAD='1',__GLX_VENDOR_LIBRARY_NAME='nvidia',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/nvidia_icd.json',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json')
probe=env.copy();probe.update(LD_LIBRARY_PATH='/tmp/coin-perf-baseline/bgfx/lib',COIN_DEBUG_GLGLUE='1')
cmd=['/tmp/coin-perf-baseline/bgfx/bin/coin_render_gl_benchmark','--backend','gl','--scene','/tmp/coin-perf-baseline/city.iv','--size','512','--warmup','0','--frames','1']
probe['COIN_GLX_PIXMAP_DIRECT_RENDERING']='1'
p=subprocess.run(cmd,env=probe,capture_output=True,text=True,timeout=180);text=p.stdout+p.stderr;(root/'coingl-physical-proof.log').write_text(text)
if p.returncode or 'NVIDIA' not in text:print(text[-2000:]);raise SystemExit('No physical NVIDIA CoinGL proof')
print('CoinGL NVIDIA proof confirmed',flush=True)
for scope in ('offscreen','window'):
 cmd=['python3','scripts/coinrender/run_performance_continuation.py','--bgfx-build','/tmp/coin-perf-final/bgfx','--wgpu-build','/tmp/coin-perf-final/wgpu','--coingl-build','/tmp/coin-perf-baseline/bgfx','--scene','/tmp/coin-perf-final/city.iv','--scope',scope,'--mode','measure','--gpu','nvidia','--size','512','--warmup','30','--frames','120','--rounds','3','--output',str(root/scope)]
 code=subprocess.call(cmd,cwd=repo,env=env)
 if code:raise SystemExit(code)
