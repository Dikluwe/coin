import os, subprocess, pathlib, json, hashlib
root=pathlib.Path('/tmp/coin-hardware-20261007'); results=[]
source=pathlib.Path('/tmp/coin-render-first-frame')
def env_for(gpu,backend='bgfx',api='vulkan'):
 build='/tmp/coin-render-first-frame-'+backend
 env=dict(os.environ,DISPLAY=':0',COIN_BGFX_RENDERER=api,COIN_SHADOW_GPU_VENDOR_PATTERN='AMD' if gpu=='amd' else 'NVIDIA',LD_LIBRARY_PATH=build+'/lib',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('radeon' if gpu=='amd' else 'nvidia')+'_icd.json',__GLX_VENDOR_LIBRARY_NAME='mesa' if gpu=='amd' else 'nvidia',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('50_mesa' if gpu=='amd' else '10_nvidia')+'.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1' if gpu=='amd' else '0')
 env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
 if gpu=='nvidia': env['__NV_PRIME_RENDER_OFFLOAD']='1'
 else: env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
 return env,build
def run(key,cmd,env,expected=0,timeout=700):
 out=root/key; out.mkdir(exist_ok=True)
 with (out/'run.log').open('w') as f:
  try: code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
  except subprocess.TimeoutExpired: code=124
 results.append(dict(key=key,command=cmd,exit=code,expected_exit=expected,environment={k:v for k,v in env.items() if k.startswith(('COIN_','__','VK_','LD_LIBRARY','DISPLAY','GDK','WAYLAND'))}))
 (root/'followup-results.json').write_text(json.dumps(results,indent=2)+'\n'); print(key,code,flush=True)
 return code
env,build=env_for('amd','wgpu'); out=root/'amd-wgpu-vulkan-final'
run(out.name,['bash','.github/scripts/qualify-coin-render-shadows-linux.sh',build,'wgpu',str(out)],env)
for gpu in ('amd','nvidia'):
 env,build=env_for(gpu)
 run(gpu+'-capacity',[build+'/bin/CoinRenderShadowReferenceTest','--gl-capacity'],env)
 env['COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU']='1'; env['COIN_RENDER_REQUIRE_GL_REFERENCE']='1'; env['COIN_RENDER_REQUIRE_GL_EIGHT_MAP_REFERENCE']='1'
 run(gpu+'-eight-native-negative',[build+'/bin/CoinRenderShadowReferenceTest','--quality','8'],env,expected=1)
for backend,api in (('bgfx','vulkan'),('bgfx','opengl'),('wgpu','vulkan')):
 env,build=env_for('amd',backend,api)
 env['COIN_RENDER_REQUIRE_'+('BGFX' if backend=='bgfx' else 'WGPU')+'_SHADOW_GPU']='1'
 run('amd-'+backend+'-'+api+'-eight',[build+'/bin/CoinRenderShadowEightMapTest'],env)
 env['COIN_RENDER_REQUIRE_CAMERA_REFERENCE']='1'
 run('amd-'+backend+'-'+api+'-camera-portable',[build+'/bin/CoinRenderCameraReuseReferenceTest'],env)
 if api=='vulkan':
  key='amd-'+backend+'-'+api+'-camera-GL-diagnostic'; (root/key).mkdir(exist_ok=True)
  env['COIN_RENDER_REQUIRE_GL_REFERENCE']='1'; env['COIN_RENDER_CAMERA_REFERENCE_IMAGES']=str(root/key)
  run(key,[build+'/bin/CoinRenderCameraReuseReferenceTest'],env,expected=1)
for backend,api in (('bgfx','vulkan'),('bgfx','opengl'),('wgpu','vulkan')):
 env,build=env_for('nvidia',backend,api)
 env['COIN_RENDER_REQUIRE_'+('BGFX' if backend=='bgfx' else 'WGPU')+'_SHADOW_GPU']='1'
 run('nvidia-'+backend+'-'+api+'-eight',[build+'/bin/CoinRenderShadowEightMapTest'],env)
# Active display only during window capture, restore exactly the prior DPMS state.
env,build=env_for('nvidia')
def xset(*args): return subprocess.check_output(['xset',*args],env=env,text=True)
before=xset('q'); (root/'display-original.log').write_text(before)
try:
 xset('+dpms'); xset('dpms','force','on'); xset('-dpms')
 for gpu in ('amd','nvidia'):
  env,build=env_for(gpu)
  run(gpu+'-p20',['python3','scripts/coinrender/run_p20_physical_matrix.py','--device',gpu,'--session','physical','--bgfx-build','/tmp/coin-render-first-frame-bgfx','--wgpu-build','/tmp/coin-render-first-frame-wgpu','--scenes-dir','/tmp/coin-p17-scenes','--output-dir',str(root/(gpu+'-p20'))],env)
finally:
 xset('+dpms' if 'DPMS is Enabled' in before else '-dpms')
 for state in ('Off','Standby','Suspend'):
  if 'Monitor is '+state in before: xset('dpms','force',state.lower())
 (root/'display-restored.log').write_text(xset('q'))
for gpu in ('amd','nvidia'):
 for scale in (1,2):
  env,build=env_for(gpu,'wgpu'); env.update(WAYLAND_DISPLAY='coin-isolated',GDK_BACKEND='wayland',GDK_SCALE=str(scale))
  key=f'{gpu}-wayland-scale{scale}'
  run(key,['python3','testsuite/qt-quarter/run_isolated.py','--server','xwayland','--weston-shell','desktop-shell.so','--weston-prefix','/tmp/coin-p16-weston','--artifacts',str(root/key/'session'),'--exec','--',build+'/bin/coin_render_wayland_smoke'],env,timeout=120)
