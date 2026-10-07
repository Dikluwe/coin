"""Temporary display-power guard; preserve and restore the original DPMS state."""
import os,sys,json,subprocess,datetime,re
from pathlib import Path
repo=Path('/tmp/coin-render-first-frame');sys.path.insert(0,str(repo/'scripts/coinrender'))
import run_performance_continuation as performance
original_run=subprocess.run
root=Path('/tmp/coin-perf-main');audit=[]
env=dict(os.environ,DISPLAY=':0')
def xset(*args):
 p=original_run(['xset',*args],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
 return p.stdout
def check(label):
 text=xset('q');audit.append(dict(label=label,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),state=text))
 (root/'dpms-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
 assert 'DPMS is Disabled' in text and 'Monitor is Off' not in text,label
def monitored_run(command,*args,**kwargs):
 measured=isinstance(command,list) and command and command[0]=='/usr/bin/time'
 if measured:check('before '+Path(command[4]).name+' '+str(len(audit)))
 p=original_run(command,*args,**kwargs)
 if measured:
  try:check('after benchmark '+str(len(audit)))
  except AssertionError:
   (root/'dpms-failed-process.log').write_text(str(p.stdout)+str(p.stderr));raise
 return p
before=xset('q');(root/'dpms-original.log').write_text(before)
try:
 xset('dpms','force','on');on=xset('q');(root/'dpms-forced-on.log').write_text(on)
 assert 'Monitor is On' in on,'Could not activate display'
 xset('-dpms');check('temporary power guard installed')
 # A short static control catches 1 Hz presentation throttling before a full campaign.
 performance.configure_variants()
 for variant in ('wgpu-vulkan-literal','bgfx-vulkan-literal','bgfx-opengl-literal'):
  key,_,backend,_=performance.animation.VARIANTS[variant];build=Path('/tmp/coin-perf-final')/key
  child=performance.environment(build,variant,'nvidia');child['DISPLAY']=':0'
  sample=root/(variant+'-active-probe.csv')
  command=[str(build/'bin/coin_render_window_benchmark'),'--backend',backend,'--scene','/tmp/coin-perf-final/city.iv','--width','512','--height','512','--warmup','5','--frames','15','--samples-output',str(sample)]
  check('before probe '+variant);p=original_run(command,env=child,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
  (root/(variant+'-active-probe.log')).write_text(p.stdout);assert p.returncode==0,(variant,p.stdout[-1500:]);assert 'NVIDIA' in p.stdout
  stats=performance.animation.csv_stats(sample,15);check('after probe '+variant)
  print('Active probe',variant,stats['total_ms'],flush=True)
  assert stats['total_ms']['median_ms']<100,'Presentation remains throttled; no full campaign started'
 subprocess.run=monitored_run
 sys.argv=['run_performance_continuation.py','--bgfx-build','/tmp/coin-perf-final/bgfx','--wgpu-build','/tmp/coin-perf-final/wgpu','--coingl-build','/tmp/coin-perf-baseline/bgfx','--scene','/tmp/coin-perf-final/city.iv','--scope','window','--mode','measure','--gpu','nvidia','--size','512','--warmup','30','--frames','120','--rounds','3','--output',str(root/'window')]
 performance.main()
 print('Active window campaign complete; starting separate verification and diagnostics',flush=True)
 for script,log in [('/tmp/coin-perf-after.py','/tmp/coin-perf-after.log'),('/tmp/coin-perf-gates.py','/tmp/coin-perf-gates.log')]:
  check('before '+script)
  with open(log,'w') as stream:original_run(['python3',script],env=env,stdout=stream,stderr=subprocess.STDOUT,check=True)
  check('after '+script)
 original_run(['python3','/tmp/coin-perf-analyze.py',str(root),'--output','/tmp/coin-perf-analysis.json'],check=True)
finally:
 subprocess.run=original_run
 if 'DPMS is Enabled' in before:xset('+dpms')
 else:xset('-dpms')
 if 'Monitor is Off' in before:xset('dpms','force','off')
 (root/'dpms-restored.log').write_text(xset('q'))
 print('Original DPMS state restored',flush=True)
original_run(['python3','/tmp/coin-perf-archive.py'],check=True)
print('Active campaign qualification and archive complete',flush=True)
