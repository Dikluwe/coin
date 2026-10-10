import pathlib,subprocess,os,json,time,hashlib,sys
r=pathlib.Path('.').resolve();out=r/(sys.argv[1] if len(sys.argv)>1 else 'native-removal-initial');out.mkdir(exist_ok=True);e={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))};e['PATH']=str(r/'install-bgfx/bin')+os.pathsep+e['PATH'];e['COIN_BGFX_RENDERER']='d3d12';rows=[]
for trial in range(int(sys.argv[2]) if len(sys.argv)>2 else 1):
 for policy in ['native','portable']:
  for mode in ['offscreen','window']:
   name=str(trial)+'-'+policy+'-'+mode;cmd=[str(r/'windows-fronts-bgfx/Release/coin_native_removal.exe'),policy,mode,str(out/name)];start=time.time()
   with (out/(name+'.log')).open('w') as f:
    try:code=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT,timeout=180).returncode
    except subprocess.TimeoutExpired:code=124
   content=(out/(name+'.log')).read_text(errors='replace'); gate=code==0 and 'before=0x00000000 after=0x887a0005 runtime_remove=1' in content and 'native_runtime_removal recovered=1 pixels_exact=1 result=1' in content and ('loaded_module name=CoinRender4.dll path='+str(r/'install-bgfx/bin/CoinRender4.dll')).lower() in content.lower()
   row={'name':name,'pass_gate':gate,'command':cmd,'exit':code,'seconds':time.time()-start,'binary_sha256':hashlib.sha256(pathlib.Path(cmd[0]).read_bytes()).hexdigest(),'log_sha256':hashlib.sha256((out/(name+'.log')).read_bytes()).hexdigest(),'environment':{k:v for k,v in e.items() if k=='PATH' or k.startswith('COIN_')},'dll_sha256':hashlib.sha256((r/'install-bgfx/bin/CoinRender4.dll').read_bytes()).hexdigest()};rows.append(row);(out/'summary.json').write_text(json.dumps(rows,indent=2));print(name,code,flush=True);print((out/(name+'.log')).read_text(errors='replace')[-1500:],flush=True)
 import xml.etree.ElementTree as E
x=E.Element('testsuite',name=out.name,tests=str(len(rows)),failures=str(sum(not v['pass_gate'] for v in rows)))
for v in rows:
 t=E.SubElement(x,'testcase',name=v['name'],time=str(v['seconds']))
 if not v['pass_gate']:E.SubElement(t,'failure',message='exit '+str(v['exit']))
E.ElementTree(x).write(out/'results.xml',encoding='utf-8',xml_declaration=True)
