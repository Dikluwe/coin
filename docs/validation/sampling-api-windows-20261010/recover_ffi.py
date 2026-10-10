import pathlib,os,subprocess,time,json,hashlib,xml.etree.ElementTree as E,shutil
r=pathlib.Path('.').resolve();out=r/'ffi-recovery';out.mkdir(exist_ok=True);rows=[]
base={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))}
for api,name in [('d3d12','dx12'),('vulkan','vulkan'),('opengl','gl')]:
 env=dict(base,PATH=str(r/'build-wgpu/bin')+os.pathsep+base['PATH'],WGPU_BACKEND=name,COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU='1',COIN_SAMPLING_STUDY='fetch')
 cmd=['ctest','--test-dir',str(r/'build-wgpu'),'-C','Release','--output-on-failure','-R','^CoinWgpuFfiFrameTest$','--output-junit',str(out/(api+'.xml'))]
 start=time.time()
 with (out/(api+'.log')).open('w',encoding='utf-8') as f:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=360).returncode
 shutil.copyfile(r/'build-wgpu/Testing/Temporary/LastTest.log',out/(api+'-last-test.log'))
 rows.append(dict(api=api,command=cmd,environment={k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_')) or k=='PATH'},exit=code,seconds=time.time()-start,binary_sha256=hashlib.sha256((r/'build-wgpu/bin/CoinWgpuFfiFrameTest.exe').read_bytes()).hexdigest()))
 print(api,'exit',code,flush=True)
(out/'summary.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
assert all(x['exit']==0 for x in rows),rows
initial=r/'suite-effective-wgpu.xml';shutil.copyfile(initial,out/'initial-effective-wgpu.xml')
t=E.parse(initial);root=t.getroot();failed=next(x for x in root if x.attrib['name']=='CoinWgpuFfiFrameTest');index=list(root).index(failed);assert failed.find('failure') is not None
root.remove(failed);new=E.parse(out/'d3d12.xml').find('.//testcase');root.insert(index,new);root.set('failures',str(sum(x.find('failure') is not None for x in root)));t.write(initial,encoding='utf-8',xml_declaration=True)
print('FFI recovered; original failure and effective ledger preserved')
