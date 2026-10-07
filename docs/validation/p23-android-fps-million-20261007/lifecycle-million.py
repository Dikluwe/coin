import subprocess,time,json,re
from pathlib import Path
out=Path(__file__).resolve().parent;adb=['/home/dikluwe/Android/Sdk/platform-tools/adb'];pkg='org.coin3d.coinrender.p23';commands=[]
def run(name,args):
 p=subprocess.run(adb+args,capture_output=True,timeout=40);(out/('lifecycle-final-'+name+'.log')).write_bytes(p.stdout+p.stderr);commands.append({'name':name,'args':args,'exit':p.returncode});(out/'lifecycle-final-commands.json').write_text(json.dumps(commands,indent=2));return p.stdout.decode(errors='replace')
def snapshot(name):
 log=run(name,['logcat','-d','-v','threadtime','CoinRenderP23:I','AndroidRuntime:E','libc:F','*:S'])
 (out/('lifecycle-final-'+name+'.png')).write_bytes(subprocess.run(adb+['exec-out','screencap','-p'],capture_output=True,check=True).stdout)
 run(name+'-memory',['shell','dumpsys','meminfo',pkg]);return log
def wait(name,predicate):
 for i in range(90):
  time.sleep(1);log=run(name,['logcat','-d','-v','threadtime','CoinRenderP23:I','AndroidRuntime:E','libc:F','*:S'])
  if 'Fatal signal' in log or 'render failed:' in log or 'offscreen capture failed:' in log:raise RuntimeError(log[-2000:])
  if predicate(log):return log
 raise RuntimeError(name+' deadline exceeded')
log=snapshot('before');assert 'scene=city-1000000 buildings=1000000' in log
pid=run('pid-before',['shell','pidof',pkg]).strip();assert pid.isdigit()
generation=max(map(int,re.findall(r'generation=(\d+) serial=',log)))
run('home',['shell','input','keyevent','3']);time.sleep(3)
run('return',['shell','am','start','-n',pkg+'/android.app.NativeActivity'])
wait('return-progress',lambda s:f'generation={generation+1} serial=' in s)
assert run('pid-return',['shell','pidof',pkg]).strip()==pid
snapshot('returned');print('HOME return passed',flush=True)
run('rotation',['shell','wm','user-rotation','lock','1']);time.sleep(2)
wait('rotation-progress',lambda s:'size=2400x1080 rgba_fnv64=' in s)
snapshot('landscape');print('Rotation passed',flush=True)
run('back',['shell','input','keyevent','4'])
wait('back-progress',lambda s:'P23 smoke finished: OK' in s)
run('launcher',['shell','am','start','-n',pkg+'/android.app.NativeActivity'])
wait('launcher-progress',lambda s:s.count('scene=city-1000000 buildings=1000000')>=2 and s.rfind('rgba_fnv64=')>s.rfind('scene=city-1000000 buildings=1000000'))
snapshot('reopened');print('BACK + launcher million selection passed',flush=True)
subprocess.run(['wmctrl','-r','Coin Render Android — 1.000.000 prédios — arraste para girar e dar zoom','-T','Coin Render Android — 1.000.000 prédios — arraste para girar e dar zoom'])
subprocess.run(['wmctrl','-a','Coin Render Android — 1.000.000 prédios — arraste para girar e dar zoom'])
