import pathlib,json,os,subprocess,hashlib
root=pathlib.Path('/tmp/coin-windows-integration-linux-20261007');records=[]
rows=[r for r in json.loads((root/'results.json').read_text()) if not r['key'].endswith('-camera')]
extra=json.loads((root/'wgpu-gl-results.json').read_text());rows.append(dict(key='amd-wgpu-opengl',environment=extra['environment']))
for row in rows:
 gpu,backend,api=row['key'].split('-');prefix=root/('relocated-sdk-'+backend);exe=root/('consumer-'+backend+'-relocated')/'coin-render-sdk-consumer';env=dict(os.environ,**row['environment']);env['LD_LIBRARY_PATH']=str(prefix/'lib');env['COIN_RENDER_TRACE_PHASES']='1';env['COIN_BGFX_TRACE_GL_ADAPTER']='1'
 folder=root/('sdk-'+row['key']);folder.mkdir(exist_ok=True)
 loader=subprocess.run(['ldd',str(exe)],env=env,text=True,capture_output=True,check=True).stdout;(folder/'loaded-libraries.log').write_text(loader)
 assert str(prefix/'lib/libCoinRender.so') in loader and str(prefix/'lib/libCoin.so.80') in loader
 cmd=[str(exe),api];p=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=90);log=p.stdout+p.stderr;(folder/'run.log').write_text(log)
 assert p.returncode==0 and 'sdk_CoinGL_rgb_max=0 reference_ready=1' in log and 'pixels_ok=1' in log,row['key']+log
 if backend=='bgfx' and api=='opengl':
  adapter=[line for line in log.splitlines() if 'bgfx_gl_adapter vendor=' in line]
  assert adapter and all(('NVIDIA' if gpu=='nvidia' else 'AMD') in line for line in adapter)
 assert not any(s in log.lower() for s in ('llvmpipe','lavapipe','swiftshader'))
 records.append(dict(key=row['key'],command=cmd,environment={k:v for k,v in env.items() if k.startswith(('COIN_','__','DISPLAY','WGPU','LD_LIBRARY','VK_'))},exit=p.returncode));print(row['key'],'SDK',p.returncode,flush=True)
 (root/'sdk-results.json').write_text(json.dumps(records,indent=2)+'\n')
for backend in ('bgfx','wgpu'):
 row=next(r for r in rows if r['key']=='nvidia-'+backend+'-vulkan');env=dict(os.environ,**row['environment']);env['LD_LIBRARY_PATH']=str(root/('relocated-sdk-'+backend)/'lib');cmd=[str(root/('consumer-'+backend+'-relocated')/'coin-render-sdk-consumer'),'d3d12'];p=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=90);(root/('sdk-'+backend+'-d3d12-negative.log')).write_text(p.stdout+p.stderr);assert p.returncode==3;records.append(dict(key=backend+'-d3d12-negative',command=cmd,exit=p.returncode,expected_exit=3));print(backend,'D3D12 rejected',p.returncode,flush=True)
(root/'sdk-results.json').write_text(json.dumps(records,indent=2)+'\n')
