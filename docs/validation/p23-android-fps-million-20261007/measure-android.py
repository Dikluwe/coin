import subprocess,time,json,re,hashlib
from pathlib import Path
out=Path(__file__).resolve().parent
adb=['/home/dikluwe/Android/Sdk/platform-tools/adb','-s','emulator-5554']
pkg='org.coin3d.coinrender.p23'; commands=[]; results=[]
def run(name,args,input=None):
 p=subprocess.run(adb+args,input=input,capture_output=True,timeout=40)
 (out/(name+'.log')).write_bytes(p.stdout+p.stderr)
 commands.append({'name':name,'args':args,'exit':p.returncode})
 (out/'ab-commands.json').write_text(json.dumps(commands,indent=2))
 return p.stdout.decode(errors='replace')
for round in range(2):
 for label,folder in [('baseline','apk-profile-baseline'),('final','apk-million-final')]:
  name=f'ab-clean-{round}-{label}'
  print('begin',name,flush=True)
  apk=out/folder/'coin-render-p23-x86_64.apk'
  assert 'Success' in run(name+'-install',['install','--no-incremental','-r',str(apk)])
  run(name+'-selection',['shell','run-as',pkg,'tee','files/coinrender-city-selection.txt'],b'200\n')
  run(name+'-rotation',['shell','wm','user-rotation','lock','1'])
  run(name+'-clear',['logcat','-c'])
  launch=run(name+'-start',['shell',f'am force-stop {pkg}; sleep 0.2; am start -W -n {pkg}/android.app.NativeActivity --ez coinrender_million_city false --ez coinrender_trace false'])
  if 'Activity not started' in launch:raise RuntimeError('warm launch excluded')
  log=''
  for i in range(90):
   time.sleep(1)
   log=run(name+'-log',['logcat','-d','-v','threadtime','CoinRenderP23:I','CoinRenderWgpu:W','AndroidRuntime:E','libc:F','*:S'])
   if re.search(r'frames=(?:[4-9]0|[1-9]\d{2,}) ',log):break
   if 'Fatal signal' in log or 'render failed:' in log:raise RuntimeError(log)
  assert 'buildings=40000 ground=1' in log
  assert 'camera_touch' not in log
  metrics=re.findall(r'frames=(\d+) .*fps_cpu_wall=([0-9.]+) render_present_mean_ms=([0-9.]+)',log)
  picked=[(int(f),float(fps),float(ms)) for f,fps,ms in metrics if 20<=int(f)<=40]
  assert len(picked)==3,(name,picked)
  capture=re.findall(r'size=(\d+x\d+) rgba_fnv64=([0-9a-f]+)',log)
  assert capture and all(size=='2400x1080' for size,_ in capture)
  result={'name':name,'apk_sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),'groups':picked,'capture':capture}
  results.append(result); (out/'ab-results.json').write_text(json.dumps(results,indent=2))
  png=subprocess.run(adb+['exec-out','screencap','-p'],capture_output=True,check=True).stdout
  (out/(name+'.png')).write_bytes(png)
  run(name+'-memory',['shell','dumpsys','meminfo',pkg])
  print(result,flush=True)
