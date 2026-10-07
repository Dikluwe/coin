import pathlib,subprocess,os,json
out=pathlib.Path('/tmp/coin-windows-integration-linux-20261007');records=[]
common=['CoinRenderFrameCoreTest','CoinRenderFrameReuseCoreTest','CoinRenderActionTest','CoinRenderNodeInventoryTest','CoinRenderPublicationTest','CoinRenderRttOwnershipTest','CoinRenderMultiTargetTest','CoinRenderMultiTargetDirectTest','CoinRenderRttProfileStagedTest','CoinRenderRttProfileDirectTest','CoinRenderRttProfilePbufferTest','CoinRenderRttProfileMipsTest','CoinRenderRttProfileMipsPbufferTest','CoinRenderRttMatrixFboTest','CoinRenderRttMatrixPbufferTest','CoinRenderSceneTextureTest','CoinRenderSceneTextureBudgetTest','CoinRenderSceneTextureDirectTest','CoinRenderSceneTextureBudgetDirectTest','CoinRenderShadowEightMapTest','CoinRenderShadowReferenceTest','CoinRenderShadowQuality8Test']
for gpu in ('amd','nvidia'):
 for backend,api in (('bgfx','vulkan'),('bgfx','opengl'),('wgpu','vulkan')):
  key=f'{gpu}-{backend}-{api}';directory=out/key;directory.mkdir(exist_ok=True);build=pathlib.Path('/tmp/coin-render-first-frame-'+backend)
  env=dict(os.environ,DISPLAY=':0',LD_LIBRARY_PATH=str(build/'lib'),COIN_BGFX_RENDERER=api,COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU='1',COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU='1',__GLX_VENDOR_LIBRARY_NAME='mesa' if gpu=='amd' else 'nvidia',__EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/'+('50_mesa' if gpu=='amd' else '10_nvidia')+'.json',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/'+('radeon' if gpu=='amd' else 'nvidia')+'_icd.json',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1' if gpu=='amd' else '0')
  env['VK_ICD_FILENAMES']=env['VK_DRIVER_FILES']
  if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
  else:env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
  tests=common+(['CoinBgfxReadbackModes_'+api] if backend=='bgfx' else ['CoinWgpuMultiDeviceTest','WgpuMultiDeviceStressTest','CoinWgpuFfiFrameTest','CoinRenderAsyncReadbackTest','CoinRenderAsyncActionTest','CoinRenderAsyncActionDirectTest'])
  regex='^('+'|'.join(tests)+')$';cmd=['ctest','--test-dir',str(build),'-R',regex,'--output-on-failure','--parallel','1','--output-junit',str(directory/'tests.xml')]
  with (directory/'tests.log').open('w') as f: code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=900).returncode
  subprocess.run([str(build/'bin/CoinRenderShadowReferenceTest'),'--gl-capacity'],env=env,stdout=(directory/'gl-capacity.log').open('w'),stderr=subprocess.STDOUT,check=True)
  records.append(dict(key=key,command=cmd,environment={k:v for k,v in env.items() if k.startswith(('COIN_','__','VK_','DISPLAY','LD_LIBRARY'))},exit=code));print(key,code,flush=True)
  env['COIN_RENDER_REQUIRE_CAMERA_REFERENCE']='1'
  if gpu=='amd' and api=='vulkan':env.pop('COIN_RENDER_REQUIRE_GL_REFERENCE',None)
  cmd=[str(build/'bin/CoinRenderCameraReuseReferenceTest')]
  with (directory/'camera.log').open('w') as f:camera=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=150).returncode
  records.append(dict(key=key+'-camera',command=cmd,requires_gl='COIN_RENDER_REQUIRE_GL_REFERENCE' in env,exit=camera));print(key,'camera',camera,flush=True)
  (out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
