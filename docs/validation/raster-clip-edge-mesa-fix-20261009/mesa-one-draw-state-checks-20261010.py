#!/usr/bin/env python3
import os, subprocess, time, pathlib, re
mesa='/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-failure-fixes/mesa-study/install/lib/x86_64-linux-gnu'
base=os.environ.copy()
base.update({'LD_LIBRARY_PATH':'/tmp/coin-mesa-finish-lib:'+mesa,
 'LIBGL_DRIVERS_PATH':'/tmp/coin-mesa-private-dri-20261009',
 'AMD_DEBUG':'nongg','MESA_SHADER_CACHE_DISABLE':'true',
 'EGL_PLATFORM':'surfaceless',
 '__EGL_VENDOR_LIBRARY_FILENAMES':'/usr/share/glvnd/egl_vendor.d/50_mesa.json'})
candidate=base|{'MESA_EXPERIMENTAL_CLIP_BOUNDARY':'1'}
native=base.copy()
log=[]
def run(name,exe,args,env):
 t=time.perf_counter()
 p=subprocess.run([exe,*args],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=90)
 secs=time.perf_counter()-t
 lines=[x for x in p.stdout.splitlines() if not x.startswith('renderer=') and not x.startswith('GL=')]
 log.append(f'{name} exit={p.returncode} elapsed={secs:.2f}s')
 log.extend(lines)
 return p.returncode,'\n'.join(lines)
checks=[]
rc,out=run('point-8','/tmp/coin-mesa-multiplane-point-20261010',['8'],candidate)
checks.append(rc==0 and all(f'p{x}=25' in out for x in ('00','10','11','01')))
for mode in ('0','1'):
 rc,out=run('depth-stencil-blend-'+mode,'/tmp/coin-mesa-depth-stencil-blend-20261010',[mode],candidate)
 checks.append(rc==0 and 'rgb=128,0,0' in out and 'depth=0.50000 stencil=1 error=0' in out)
for mode in ('line','point'):
 exe='/tmp/coin-mesa-cull-'+mode
 a,b=run('cull-'+mode+'-candidate',exe,['2'],candidate)
 c,d=run('cull-'+mode+'-native',exe,['2'],native)
 checks.append(a==c==0 and b==d)
for mode in ('0','1'):
 exe='/tmp/coin-mesa-color-state-20261010'
 a,b=run('flat-'+mode+'-candidate',exe,[mode,'1'],candidate)
 c,d=run('flat-'+mode+'-native',exe,[mode,'1'],native)
 checks.append(a==c==0 and b==d)
for name,exe in [('edgeflag','/tmp/coin-mesa-edgeflag-20261010'),('primitives-query','/tmp/coin-mesa-query-20261010')]:
 a,b=run(name+'-candidate',exe,['2'],candidate)
 c,d=run(name+'-native',exe,['2'],native)
 checks.append(a==c and 'error=0' in b and b==d)
log.append(f'CHECKS {sum(checks)}/{len(checks)} PASS')
pathlib.Path('/tmp/coin-mesa-final-states-20261010.log').write_text('\n'.join(log)+'\n')
print('\n'.join(log))
raise SystemExit(0 if all(checks) else 1)
