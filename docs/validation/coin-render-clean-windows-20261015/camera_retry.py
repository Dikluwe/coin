import pathlib,json,os,subprocess,time,shutil,sys,hashlib
r=pathlib.Path(__file__).parent;p=r/'camera-checks.json';shutil.copy2(p,r/'camera-initial-checks.json');rows=json.loads(p.read_text())
row=next(x for x in rows if x['label']=='camera-bgfx-vulkan')
env={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))};env.update(row['selected_environment']);env['PATH']=str(r/'install-bgfx/bin')+os.pathsep+env['PATH']
log=r/'camera-bgfx-vulkan-retry.log';t=time.time()
with log.open('w',encoding='utf-8') as f:code=subprocess.run(row['command'],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180).returncode
row.update(exit=code,seconds=time.time()-t,log=log.name,sha256=hashlib.sha256(log.read_bytes()).hexdigest(),retry_reason='Initial process returned zero with an empty log and no physical API receipt; it is not qualification evidence.')
p.write_text(json.dumps(rows,indent=2))
with (r/'final-analysis.log').open('w') as f:subprocess.run([sys.executable,str(r/'analyze.py')],cwd=r,stdout=f,stderr=subprocess.STDOUT,check=True)
print('Camera BGFX Vulkan retry exit',code,'log bytes',log.stat().st_size)
