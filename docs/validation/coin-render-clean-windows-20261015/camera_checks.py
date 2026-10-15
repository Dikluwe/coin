import pathlib,sys,os,subprocess,json,time
r=pathlib.Path(__file__).parent;rows=[]
for mode in ['bgfx','wgpu']:
 for api in ['d3d12','vulkan','opengl']:
  e={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))};e['PATH']=str(r/('install-'+mode)/'bin')+os.pathsep+e['PATH'];e.update(COIN_BGFX_RENDERER=api,WGPU_BACKEND={'d3d12':'dx12','vulkan':'vulkan','opengl':'gl'}[api],COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1',COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU='1',COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU='1')
  label='camera-'+mode+'-'+api;cmd=[str(r/('build-'+mode)/'bin/CoinRenderCameraReuseReferenceTest.exe')];t=time.time();print(label,'started',flush=True)
  with (r/(label+'.log')).open('w',encoding='utf-8') as f:
   try:code=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT,timeout=180).returncode
   except subprocess.TimeoutExpired:code=124
  rows.append({'label':label,'command':cmd,'exit':code,'seconds':time.time()-t,'selected_environment':{k:v for k,v in e.items() if k.startswith(('COIN_','WGPU_'))}});(r/'camera-checks.json').write_text(json.dumps(rows,indent=2));print(label,'exit',code,flush=True)
