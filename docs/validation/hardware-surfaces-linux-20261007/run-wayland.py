import os,pathlib,subprocess,json
root=pathlib.Path('/tmp/coin-hardware-20261007'); results=[]
for gpu in ('amd','nvidia'):
 env=dict(os.environ,DISPLAY=':0',LD_LIBRARY_PATH='/tmp/coin-render-first-frame-wgpu/lib',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('radeon' if gpu=='amd' else 'nvidia')+'_icd.json',__GLX_VENDOR_LIBRARY_NAME='mesa' if gpu=='amd' else 'nvidia',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('50_mesa' if gpu=='amd' else '10_nvidia')+'.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1' if gpu=='amd' else '0')
 env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
 if gpu=='nvidia': env['__NV_PRIME_RENDER_OFFLOAD']='1'
 else: env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
 for name in ('GDK_BACKEND','GDK_SCALE','WAYLAND_DISPLAY'): env.pop(name,None)
 for scale in (1,2):
  key=f'{gpu}-wayland-scale{scale}-final'; out=root/key; out.mkdir(exist_ok=True)
  cmd=['python3','testsuite/qt-quarter/run_isolated.py','--server','xwayland','--weston-shell','desktop-shell.so','--weston-prefix','/tmp/coin-p16-weston','--artifacts',str(out/'session'),'--exec','--','env','DISPLAY=:0','WAYLAND_DISPLAY=coin-isolated','GDK_BACKEND=wayland',f'GDK_SCALE={scale}','COIN_RENDER_REQUIRE_GL_REFERENCE=1','/tmp/coin-render-first-frame-wgpu/bin/coin_render_wayland_smoke']
  with (out/'run.log').open('w') as f:
   try: code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=120).returncode
   except subprocess.TimeoutExpired: code=124
  print(key,code,flush=True)
  results.append(dict(key=key,command=cmd,exit=code,environment={k:v for k,v in env.items() if k.startswith(('COIN_','__','VK_','LD_LIBRARY','DISPLAY'))}))
  (root/'wayland-results.json').write_text(json.dumps(results,indent=2)+'\n')
