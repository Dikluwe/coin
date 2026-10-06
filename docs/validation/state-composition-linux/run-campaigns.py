import json,os,signal,subprocess,sys,time
from datetime import datetime,timezone
from pathlib import Path
root=Path('/tmp/coin-render-first-frame')
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
common=['python3','/tmp/coin-render-all-matched.py','--before-bgfx-build','/tmp/coin-render-state-baseline/bgfx','--after-bgfx-build','/tmp/coin-render-first-frame-bgfx','--before-wgpu-build','/tmp/coin-render-state-baseline/wgpu','--after-wgpu-build','/tmp/coin-render-first-frame-wgpu','--coingl-build','/tmp/coin-render-material-geometry-baseline/coingl','--scene','/tmp/coin-render-city-40000.iv','--variants','coingl,bgfx-vulkan,bgfx-opengl,wgpu-vulkan','--rounds','3','--gpu','nvidia','--size','1024','--before-source-content-revision','9737b2e1e60eba6b44988d684c2838271d99dc05','--after-source-content-revision',revision,'--control-source-content-revision','4d63bb993022ee8d40802558b0871a4803002b8d','--runner',str(root/'scripts/coinrender/run_animation_benchmark.py'),'--row-helper','/tmp/coin-render-wgpu-motion-matched.py']
observations=[]
def display_active():
 entry={'utc':datetime.now(timezone.utc).isoformat()}
 for key,cmd in [('display',['xset','q']),('screensaver',['dbus-send','--session','--print-reply','--dest=org.cinnamon.ScreenSaver','/org/cinnamon/ScreenSaver','org.cinnamon.ScreenSaver.GetActive'])]:
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=8)
  text=p.stdout+p.stderr
  if key=='display' and 'Screen Saver:' in text:text='Screen Saver:'+text.split('Screen Saver:',1)[1]
  entry[key]={'argv':cmd,'exit_code':p.returncode,'output':text}
 entry['active']=entry['display']['exit_code']==0 and 'Monitor is On' in entry['display']['output'] and entry['screensaver']['exit_code']==0 and 'boolean false' in entry['screensaver']['output']
 observations.append(entry)
 Path('/tmp/coin-render-state-display-observations.json').write_text(json.dumps(observations,indent=2)+'\n')
 return entry['active']
for name,scope,cases,frames,warmup in [('offscreen','offscreen','transforms-10,materials-10,geometry-10,static,camera','15','5'),('stress','offscreen','geometry-100','7','3'),('window','window','transforms-10,materials-10,geometry-10','15','3')]:
 cmd=common+['--warmup',warmup,'--scope',scope,'--cases',cases,'--frames',frames,'--output-before','/tmp/coin-render-state-'+name+'-before','--output-after','/tmp/coin-render-state-'+name+'-after']
 if scope=='window' and not display_active():
  Path('/tmp/coin-render-state-window-exclusion.json').write_text(json.dumps({'reason':'Display not active before campaign','observations':observations},indent=2)+'\n')
  print('EXCLUDED',name,flush=True);continue
 print('CAMPAIGN',name,flush=True)
 with Path('/tmp/coin-render-state-'+name+'-run.log').open('w') as log:
  process=subprocess.Popen(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,text=True,start_new_session=True)
  if scope!='window':code=process.wait()
  else:
   excluded=False
   while process.poll() is None:
    if not display_active():
     os.killpg(process.pid,signal.SIGTERM);excluded=True
     try:process.wait(timeout=10)
     except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
     break
    try:process.wait(timeout=10)
    except subprocess.TimeoutExpired:pass
   code=process.returncode
   if not excluded and not display_active():excluded=True
   if excluded:
    Path('/tmp/coin-render-state-window-exclusion.json').write_text(json.dumps({'reason':'Display not active during or after campaign; all window timings excluded','observations':observations},indent=2)+'\n')
    print('EXCLUDED',name,flush=True);continue
 if code:sys.exit(code)
 print('FINISHED',name,flush=True)
