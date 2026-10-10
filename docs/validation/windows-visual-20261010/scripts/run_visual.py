import os,pathlib,json,subprocess,time,hashlib,sys
r=pathlib.Path('.').resolve();out=r/(sys.argv[1] if len(sys.argv)>1 else 'window-campaign-pilot');out.mkdir(exist_ok=True);frames=int(sys.argv[2]) if len(sys.argv)>2 else 1;warmup=int(sys.argv[3]) if len(sys.argv)>3 else 1;workloads=sys.argv[4].split(',') if len(sys.argv)>4 else ['city-40000','terrain-million','solids'];ordered=['native','portable'];rows=[]
for work in workloads:
 for backend in (sys.argv[5].split(',') if len(sys.argv)>5 else ['bgfx','wgpu']):
  for api,ordinal in [('d3d12',4),('vulkan',1),('opengl',2)]:
   for index,policy in enumerate(ordered):
    name=f'{work}-{backend}-{api}-{index}-{policy}';env={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))};env['PATH']=str(r/('install-'+backend)/'bin')+os.pathsep+env['PATH'];env['COIN_BGFX_RENDERER']=api;env['WGPU_BACKEND']={'d3d12':'dx12','vulkan':'vulkan','opengl':'gl'}[api];env['COIN_BGFX_TRACE_GL_ADAPTER']='1';
    if len(sys.argv)>6 and sys.argv[6]=='static':env['COIN_VISUAL_STATIC_CAMERA']='1'
    cmd=[str(r/('visual-'+backend)/'Release/coin_visual_campaign.exe'),api,policy,work,str(frames),str(warmup),str(out/name)];started=time.time();print(name,'started',flush=True)
    clocks=out/(name+'-clocks.csv');cf=clocks.open('w');monitor=subprocess.Popen(['nvidia-smi','--query-gpu=timestamp,name,pci.bus_id,driver_version,pstate,clocks.current.graphics,clocks.current.memory,temperature.gpu,utilization.gpu,power.draw','--format=csv','-lms','1000'],stdout=cf,stderr=subprocess.STDOUT)
    with (out/(name+'.log')).open('w') as f:
     try:code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=900).returncode
     except subprocess.TimeoutExpired:code=124
    monitor.terminate();monitor.wait();cf.close();content=(out/(name+'.log')).read_text(errors='replace');gate=code==0 and f'receipt renderer={ordinal} vendor=10de' in content and 'window_campaign result=1' in content and 'readback_during_measurement=none' in content and 'surface_oracle stage=final rgb_max=0 exact=1' in content
    gate=gate and ('loaded_module name=CoinRender4.dll path='+str(r/('install-'+backend)/'bin/CoinRender4.dll')).lower() in content.lower()
    row={'label':name,'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_')) or k=='PATH'},'exit':code,'pass_gate':gate,'seconds':time.time()-started,'log':name+'.log','log_sha256':hashlib.sha256((out/(name+'.log')).read_bytes()).hexdigest(),'dll_sha256':hashlib.sha256((r/('install-'+backend)/'bin/CoinRender4.dll').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(pathlib.Path(cmd[0]).read_bytes()).hexdigest()};rows.append(row);(out/'summary.json').write_text(json.dumps(rows,indent=2));print(name,'exit',code,'gate',gate,flush=True)
    if not gate:print(content[-1600:],flush=True)
import xml.etree.ElementTree as E
x=E.Element('testsuite',name=out.name,tests=str(len(rows)),failures=str(sum(not v['pass_gate'] for v in rows)))
for v in rows:
 t=E.SubElement(x,'testcase',name=v['label'],time=str(v['seconds']));
 if not v['pass_gate']:E.SubElement(t,'failure',message='exit '+str(v['exit']))
 E.SubElement(t,'system-out').text=v['log']+' SHA256 '+v['log_sha256']
E.ElementTree(x).write(out/'results.xml',encoding='utf-8',xml_declaration=True)
