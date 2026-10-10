import argparse, hashlib, json, os, pathlib, subprocess, time, shutil, re, xml.etree.ElementTree as ET
ROOT=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--phase',choices=['initial','sdk','sampling','npot','window','suite','policy-window','latency','camera'],required=True)
p.add_argument('--backend',choices=['bgfx','wgpu','both'],default='both')
p.add_argument('--name')
p.add_argument('--cases',help='optional comma-separated sampling cases')
a=p.parse_args();out=ROOT/(a.name or (a.phase+('-'+a.backend if a.backend!='both' else '')));out.mkdir(exist_ok=True)
base={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))}
rows=[]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(label,cmd,env):
    start=time.time(); print(label,'started',flush=True)
    monitor=None;monitor_file=None;monitor_command=None
    if a.phase in ['npot','latency']:
        monitor_command=['nvidia-smi','--query-gpu=timestamp,name,pci.bus_id,driver_version,pstate,temperature.gpu,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw','--format=csv','-lms','1000']
        monitor_file=(out/(label+'-clocks.csv')).open('w',encoding='utf-8')
        try:monitor=subprocess.Popen(monitor_command,stdout=monitor_file,stderr=subprocess.STDOUT)
        except FileNotFoundError:monitor_file.write('UNAVAILABLE nvidia-smi\n')
    with (out/(label+'.log')).open('w',encoding='utf-8') as f:
        try:code=subprocess.run(cmd,env=env,cwd=out,stdout=f,stderr=subprocess.STDOUT,timeout=1800 if a.phase=='suite' else 360).returncode
        except subprocess.TimeoutExpired:code=124
    if monitor:
        monitor.terminate();monitor.wait(timeout=10)
    if monitor_file:monitor_file.close()
    row=dict(label=label,command=cmd,exit=code,seconds=time.time()-start,log=label+'.log',
        log_sha256=sha(out/(label+'.log')),environment={k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_')) or k=='PATH'})
    content=(out/(label+'.log')).read_text(encoding='utf-8',errors='replace')
    if monitor_command:row['clock_monitor_command']=monitor_command
    row['pass_gate']=code==0
    if '-sdk-' in label and not label.endswith('-c11'):
        api=label.split('-')[1];expected={'d3d12':4,'vulkan':1,'opengl':2}[api]
        row['receipt_gate']=bool(re.search(r'installed_sdk renderer='+str(expected)+r' vendor_id=0x(?:10de|8086|1002)\b.*pixels_ok=1',content))
        row['pass_gate']=row['pass_gate'] and row['receipt_gate'] and 'reference_ready=1' in content
        if a.phase=='sdk':row['pass_gate']=row['pass_gate'] and 'sdk_sampling policy='+('1' if label.endswith('portable') else '0') in content and 'result=1' in content
    if pathlib.Path(cmd[0]).is_file():row['binary_sha256']=sha(pathlib.Path(cmd[0]))
    rows.append(row);(out/'summary.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    print(label,'exit',code,flush=True)
    if code:print((out/(label+'.log')).read_text(encoding='utf-8',errors='replace')[-1800:],flush=True)
for backend in ['bgfx','wgpu']:
    if a.backend!='both' and backend!=a.backend:continue
    build=ROOT/('build-'+backend);prefix=ROOT/('install-'+backend)
    if a.phase in ['sdk','initial']:
        hashes={n:dict(build=sha(build/'bin'/n),installed=sha(prefix/'bin'/n)) for n in ['Coin4.dll','CoinRender4.dll']}
        assert all(v['build']==v['installed'] for v in hashes.values())
        (out/(backend+'-dll-hashes.json')).write_text(json.dumps(hashes,indent=2),encoding='utf-8')
        if a.phase=='sdk':
            cexe=next((ROOT/('consumer-'+backend)).rglob('coin-render-sdk-capabilities-c.exe'))
            cenv=dict(base);cenv['PATH']=str(prefix/'bin')+os.pathsep+base['PATH']
            run(backend+'-sdk-c11',[str(cexe)],cenv)
    for api in ['d3d12','vulkan','opengl']:
        env=dict(base);env['PATH']=str((prefix if a.phase in ['initial','sdk'] else build)/'bin')+os.pathsep+base['PATH']
        env['COIN_BGFX_RENDERER']=api;env['WGPU_BACKEND']={'d3d12':'dx12','vulkan':'vulkan','opengl':'gl'}[api]
        env['COIN_SAMPLING_STUDY']='fetch';env['COIN_BGFX_TRACE_GL_ADAPTER']='1'
        env['COIN_RENDER_REQUIRE_GL_REFERENCE']='1';env['COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU']='1';env['COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU']='1'
        stem=backend+'-'+api
        if a.phase in ['initial','sdk']:
            exe=next((ROOT/('consumer-'+backend)).rglob('coin-render-sdk-consumer.exe'))
            policies=['native'] if a.phase=='initial' else ['native','portable']
            for policy in policies:
                cmd=[str(exe),api]+([] if a.phase=='initial' else [policy,str(out/(stem+'-'+policy))])
                run(stem+'-sdk-'+policy,cmd,env)
            if a.phase=='initial':run(stem+'-sampling-api',[str(build/'bin/CoinRenderSamplingPolicyTest.exe'),'--gpu'],env)
        if a.phase=='sampling':
            cases={'api':['CoinRenderSamplingPolicyTest','--gpu'], 'projective':['CoinRenderTextureSamplingTest','--projective-study'],
                   'sampling':['CoinRenderTextureSamplingTest','--gpu'],'advanced':['CoinRenderAdvancedTextureTest','--gpu'],
                   'deep':['CoinRenderAdvancedTextureTest','--gpu','--sampling-deep-study'],
                   'direct':['CoinRenderAdvancedTextureTest','--gpu','--sampling-rtt-study'],
                   'viewport':['CoinRenderAdvancedTextureTest','--gpu','--sampling-viewport-study'],
                   'procedural':['CoinRenderProceduralTextureTest','--gpu'],'rtt':['CoinRenderRttProfileTest','--mips-direct']}
            for case,arguments in cases.items():
                if a.cases and case not in a.cases.split(','):continue
                for policy in (['both'] if case=='api' else ['native','portable']):
                    env['COIN_RENDER_SAMPLING_PIXEL_PREFIX']=str(out/(stem+'-'+policy+'-'+case))
                    run(stem+'-'+policy+'-'+case,[str(build/'bin'/(arguments[0]+'.exe')),*arguments[1:],*(['--portable-sampling'] if policy=='portable' else [])],env)
        if a.phase=='npot' and backend=='bgfx':
            for mechanism in ['1','2']:
                traced=dict(env,COIN_BGFX_D3D12_QUERY_LOG=str(out/(stem+'-npot-'+mechanism+'-queries.log')),COIN_RENDER_TRACE_PHASES='1',COIN_WGPU_GPU_TIMESTAMPS='1',COIN_RENDER_NPOT_PIXEL_PREFIX=str(out/(stem+'-npot-'+mechanism)))
                run(stem+'-npot-'+mechanism,[str(build/'bin/CoinRenderShadowReferenceTest.exe'),'--npot-shadow-oit-bench',mechanism],traced)
        if a.phase=='window':
            args=[] if api=='d3d12' else ['--'+api]
            if backend=='wgpu' and api=='opengl':args+=['--expect-no-window-readback']
            run(stem+'-window',[str(build/'bin/coin_render_win32_smoke.exe'),*args],env)
        if a.phase=='policy-window':
            env['COIN_RENDER_SAMPLING_PIXEL_PREFIX']=str(out/(stem+'-window'))
            run(stem+'-policy-window',[str(build/'bin/CoinRenderSamplingPolicyTest.exe'),'--window'],env)
        if a.phase=='camera':
            env['COIN_RENDER_REQUIRE_CAMERA_REFERENCE']='1'
            env['COIN_WGPU_REQUIRE_GL_REFERENCE']='1'
            run(stem+'-camera',[str(build/'bin/CoinRenderCameraReuseReferenceTest.exe')],env)
        if a.phase=='latency':
            for i,policy in enumerate(['native','portable','portable','native']):
                label=stem+'-'+str(i)+'-'+policy
                run(label,[str(build/'bin/CoinRenderSamplingPolicyTest.exe'),'--window','--benchmark-no-readback',policy,str(out/(label+'.csv'))],env)
        if a.phase=='suite' and api=='d3d12':
            run(stem+'-ctest',['ctest','--test-dir',str(build),'-C','Release','--output-on-failure','--output-junit',str(out/(stem+'.xml'))],env)
            last=build/'Testing/Temporary/LastTest.log'
            if last.is_file():shutil.copyfile(last,out/(stem+'-last-test.log'))
tests=ET.Element('testsuite',name=a.phase,tests=str(len(rows)),failures=str(sum(not r['pass_gate'] and r['exit']!=77 for r in rows)),skipped=str(sum(r['exit']==77 for r in rows)))
for r in rows:
    t=ET.SubElement(tests,'testcase',name=r['label'],time=str(r['seconds']))
    if r['exit']==77:ET.SubElement(t,'skipped',message='exit 77')
    elif not r['pass_gate']:ET.SubElement(t,'failure',message='exit '+str(r['exit'])+' or missing execution receipt')
    ET.SubElement(t,'system-out').text=r['log']+' SHA256 '+r['log_sha256']
ET.ElementTree(tests).write(out/'results.xml',encoding='utf-8',xml_declaration=True)
raise SystemExit(0 if all(r['pass_gate'] for r in rows) else 1)
