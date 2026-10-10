import pathlib,difflib,subprocess,os,json,time,shutil
r=pathlib.Path(r'H:\Git\coin\build\sampling-win-20261010');src=r/'bgfx-device-loss-source/bgfx/src';p=src/'renderer_d3d12.cpp';s=p.read_text();s=s.replace('#include "bgfx_p.h"','#include "bgfx_p.h"\n#include <cstdio>\n#include <cstdlib>',1);p.write_text(s,newline='\n');old=pathlib.Path(r'H:\Git\coin\build\bgfx-windows-source\bgfx\src');(r/'bgfx-device-loss.patch').write_text(''.join(''.join(difflib.unified_diff((old/n).read_text().splitlines(True),(src/n).read_text().splitlines(True),fromfile='a/bgfx/src/'+n,tofile='b/bgfx/src/'+n)) for n in ['renderer_d3d12.cpp','renderer_d3d12.h']));shutil.copyfile(r/'bgfx-device-loss-history-build.log',r/'bgfx-device-loss-history-initial-include-failure.log')
e={k.upper():v for k,v in os.environ.items()};e['MSBUILDDISABLENODEREUSE']='1';rows=[]
for name,cmd in [('build',['cmake','--build',str(r/'bgfx-device-loss-build'),'--config','Release','--parallel','2']),('install',['cmake','--install',str(r/'bgfx-device-loss-build'),'--config','Release'])]:
 with (r/('bgfx-device-loss-history-fixed-'+name+'.log')).open('w') as f:code=subprocess.run(cmd,env=e,stdout=f,stderr=subprocess.STDOUT).returncode
 rows.append(dict(command=cmd,exit=code));(r/'bgfx-device-loss-history-fixed-ledger.json').write_text(json.dumps(rows,indent=2));print(name,code,flush=True)
 if code:raise SystemExit(code)
