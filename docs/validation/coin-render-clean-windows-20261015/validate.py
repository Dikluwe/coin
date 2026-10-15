import argparse,pathlib,subprocess,json,os,time,hashlib,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--modes',default='off,recording,bgfx,wgpu');p.add_argument('--tag',default='');a=p.parse_args()
r=pathlib.Path(__file__).parent;s=pathlib.Path(a.source);old=r.parent/'sampling-win-20261010';dep=old/'bgfx-device-loss-install'
env={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))};env.update(MSBUILDDISABLENODEREUSE='1',CARGO_BUILD_JOBS='2')
rows=[]
def run(label,cmd,e=None,timeout=3600):
 label=(a.tag+'-' if a.tag else '')+label
 print(label,'started',flush=True);t=time.time();log=r/(label+'.log')
 with log.open('w',encoding='utf-8') as f:
  try:code=subprocess.run(cmd,cwd=r,env=e or env,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
  except subprocess.TimeoutExpired:code=124
 rows.append({'label':label,'command':cmd,'exit':code,'seconds':time.time()-t,'log':log.name,'sha256':hashlib.sha256(log.read_bytes()).hexdigest()});(r/((a.tag+'-' if a.tag else '')+'ledger.json')).write_text(json.dumps(rows,indent=2))
 print(label,'exit',code,'seconds',round(time.time()-t),flush=True)
 if code:print(log.read_text(encoding='utf-8',errors='replace')[-4500:],flush=True)
 return code
(r/'source-sha.txt').write_bytes(subprocess.check_output(['git','-C',str(s),'rev-parse','HEAD'],env=env))
(r/'source-initial.diff').write_bytes(subprocess.check_output(['git','-C',str(s),'diff','--binary'],env=env))
if a.tag:
 (r/(a.tag+'-source-sha.txt')).write_bytes((r/'source-sha.txt').read_bytes())
 (r/(a.tag+'-source.diff')).write_bytes((r/'source-initial.diff').read_bytes())
for mode,backend in [('off',None),('recording','RECORDING'),('bgfx','BGFX'),('wgpu','RUST_BRIDGE')]:
 if mode not in a.modes.split(','):continue
 b=r/('build-'+mode);prefix=r/('install-'+mode)
 cmd=['cmake','-S',str(s),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-DCOIN_BUILD_SHARED_LIBS=ON','-DCOIN_BUILD_TESTS=ON','-DCOIN_BUILD_LEGACY_GL_RENDERER=ON','-DCMAKE_CXX_STANDARD=11','-DCOIN_BUILD_MSVC_MP=OFF','-DCMAKE_CXX_FLAGS=/MP2','-DCOIN_BUILD_DOCUMENTATION=OFF','-DCOIN_BUILD_AWESOME_DOCUMENTATION=OFF','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCOIN_BUILD_RENDER='+('ON' if backend else 'OFF'),'-DCOIN_INSTALL_RENDER_EXPERIMENTAL='+('ON' if backend else 'OFF')]
 if backend:cmd+=['-DCOIN_RENDER_BACKEND='+backend]
 cmd+=['-DCOIN_BUILD_RENDER_WINDOW_EXAMPLE='+('ON' if mode in ['bgfx','wgpu'] else 'OFF'),'-DCOIN_BUILD_RENDER_BENCHMARKS='+('ON' if mode in ['bgfx','wgpu'] else 'OFF')]
 if mode=='bgfx':cmd+=['-DCMAKE_PREFIX_PATH='+str(dep),'-Dbgfx_DIR='+str(dep/'lib/cmake/bgfx'),'-DCOIN_BGFX_SHADERC_EXECUTABLE='+str(dep/'bin/shaderc.exe'),'-DCOIN_BGFX_SHADER_INCLUDE_DIR='+str(dep/'include/bgfx')]
 if run(mode+'-configure',cmd):continue
 if run(mode+'-build',['cmake','--build',str(b),'--config','Release','--parallel','2']):continue
 if run(mode+'-install',['cmake','--install',str(b),'--config','Release']):continue
 e=dict(env);e['PATH']=str(prefix/'bin')+os.pathsep+env['PATH'];e['COIN_BGFX_RENDERER']='d3d12';e['WGPU_BACKEND']='dx12'
 if mode=='recording':e['COIN_RENDER_REQUIRE_GL_REFERENCE']='1'
 if mode in ['bgfx','wgpu']:e.update(COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU='1',COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU='1')
 run(mode+'-ctest',['ctest','--test-dir',str(b),'-C','Release','--output-on-failure','-j','1','--output-junit',str(r/(mode+'-ctest.xml'))],e,3600)
 if mode not in ['bgfx','wgpu']:continue
 cb=r/('consumer-'+mode)
 if run(mode+'-consumer-configure',['cmake','-S',str(s/'examples/coinrender/sdk-consumer'),'-B',str(cb),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_PREFIX_PATH='+str(prefix)]):continue
 if run(mode+'-consumer-build',['cmake','--build',str(cb),'--config','Release','--parallel','2']):continue
 run(mode+'-sdk-c11',[str(cb/'Release/coin-render-sdk-capabilities-c.exe')],e)
 for api in ['d3d12','vulkan','opengl']:
  e['COIN_BGFX_RENDERER']=api;e['WGPU_BACKEND']={'d3d12':'dx12','vulkan':'vulkan','opengl':'gl'}[api]
  for policy in ['native','portable']:
   run(mode+'-sdk-'+api+'-'+policy,[str(cb/'Release/coin-render-sdk-consumer.exe'),api,policy,str(r/(mode+'-'+api+'-'+policy))],e,360)
 hashes={}
 for dll in ['Coin4.dll','CoinRender4.dll']:
  hashes[dll]={key:hashlib.sha256(path.read_bytes()).hexdigest() for key,path in [('build',b/'bin'/dll),('installed',prefix/'bin'/dll)]}
 (r/(mode+'-dll-hashes.json')).write_text(json.dumps(hashes,indent=2));assert all(v['build']==v['installed'] for v in hashes.values())
print('matrix complete',flush=True)
