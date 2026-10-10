import pathlib,subprocess,os,json
r=pathlib.Path(__file__).parent
e={k.upper():v for k,v in os.environ.items()};e['MSBUILDDISABLENODEREUSE']='1';rows=[]
for b in ['bgfx','wgpu']:
 for name,cmd in [('configure',['cmake','-S',str(r/'visual-src'),'-B',str(r/('visual-'+b)),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_PREFIX_PATH='+str(r/('install-'+b))]),('build',['cmake','--build',str(r/('visual-'+b)),'--config','Release','--parallel','2'])]:
  log=r/('visual-'+b+'-'+name+'-normalized.log')
  with log.open('w') as f:code=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT).returncode
  rows.append({'backend':b,'command':cmd,'exit':code,'log':log.name});print(b,name,code,flush=True)
  if code:print(log.read_text(errors='replace')[-3200:],flush=True);break
(r/'visual-build-ledger.json').write_text(json.dumps(rows,indent=2))
