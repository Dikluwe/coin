import subprocess,time,json,sys,re
from pathlib import Path
out=Path(__file__).resolve().parent;adb=['/home/dikluwe/Android/Sdk/platform-tools/adb'];pkg='org.coin3d.coinrender.p23'
trace='--trace' in sys.argv; million='--40k' not in sys.argv; name='final-'+('million' if million else '40k')+('-trace' if trace else '-normal');cmds=[]
def run(suffix,args):
 p=subprocess.run(adb+args,capture_output=True,timeout=40);(out/(name+'-'+suffix+'.log')).write_bytes(p.stdout+p.stderr);cmds.append({'name':suffix,'args':args,'exit':p.returncode});(out/(name+'-commands.json')).write_text(json.dumps(cmds,indent=2));return p.stdout.decode(errors='replace')
if '--install' in sys.argv:assert 'Success' in run('install',['install','--no-incremental','-r',str(out/'apk-final/coin-render-p23-x86_64.apk')])
run('clear',['logcat','-c']);run('trace-remove',['shell','run-as',pkg,'rm','-f','files/coinrender-phases.log'])
start=run('start',['shell',f'am start -S -W -n {pkg}/android.app.NativeActivity --ez coinrender_million_city {str(million).lower()} --ez coinrender_trace {str(trace).lower()} --ez coinrender_continuous {str(not trace).lower()}'])
assert 'Activity not started' not in start
pid=run('pid',['shell','pidof',pkg]).strip();assert pid.isdigit(),pid
for i in range(120):
 time.sleep(1);log=run('native',['logcat','-d','--pid='+pid,'-v','threadtime'])
 if 'Fatal signal' in log or 'render failed:' in log or 'offscreen capture failed:' in log:raise RuntimeError(log[-2000:])
 if (trace and 'rgba_fnv64=' in log) or (not trace and re.search(r'frames=(?:[2-9]00|[1-9]\d{3,}) ',log)):break
 if i%5==0 and run('pid',['shell','pidof',pkg]).strip()!=pid:
  run('failure-full',['logcat','-d','-v','threadtime']);raise RuntimeError('Original process gone; exclude retry/manual launches')
else:raise RuntimeError('Deadline exceeded')
assert f'buildings={1000000 if million else 40000} ground=1' in log
if trace:
 time.sleep(2);text=run('phases',['shell','run-as',pkg,'cat','files/coinrender-phases.log'])
 assert f'instances={1000001 if million else 40001}' in text
 assert f'instance_bytes={96000096 if million else 3840096}' in text
 print(text[-1800:],flush=True)
else:
 assert 'camera_touch' not in log
 metrics=re.findall(r'frames=(\d+) .*fps_cpu_wall=([0-9.]+) render_present_mean_ms=([0-9.]+)',log)
 (out/(name+'-samples.json')).write_text(json.dumps(metrics,indent=2));print(metrics[-10:],flush=True)
run('memory',['shell','dumpsys','meminfo',pkg])
(out/(name+'.png')).write_bytes(subprocess.run(adb+['exec-out','screencap','-p'],capture_output=True,check=True).stdout)
print(log[-1800:],flush=True)
