import subprocess,time,json,struct
from pathlib import Path
campaign=Path('/mnt/Laranja/Git/externos/coin-render-artifacts/p23-city-20261007');out=campaign/'final-qualification';out.mkdir(exist_ok=True);adb=['/home/dikluwe/Android/Sdk/platform-tools/adb','-s','emulator-5554'];commands=[]
def run(name,args,png=False):
 p=subprocess.run(adb+args,capture_output=True);(out/(name+('.png' if png else '.log'))).write_bytes(p.stdout if png else p.stdout+p.stderr);commands.append({'name':name,'command':args,'exit':p.returncode});(out/'qualified-commands.json').write_text(json.dumps(commands,indent=2));print(name,p.returncode,flush=True);return p.stdout.decode(errors='replace')
def log(name):return run(name,['logcat','-d','-v','threadtime','CoinRenderP23:I','CoinRenderWgpu:I','AndroidRuntime:E','libc:F','*:S'])
def start(name,extra=[]):run(name,['shell','am','start','-W','-a','android.intent.action.MAIN','-c','android.intent.category.LAUNCHER','-n','org.coin3d.coinrender.p23/android.app.NativeActivity']+extra)
run('qualified-install',['install','--no-incremental','-r',str(campaign/'apk-controls-x86_64/coin-render-p23-x86_64.apk')])
run('qualified-stop',['shell','am','force-stop','org.coin3d.coinrender.p23'])
for i in range(20):
 if not run('qualified-old-pid-'+str(i),['shell','pidof','org.coin3d.coinrender.p23']).strip():break
 time.sleep(.5)
else:raise RuntimeError('old process survived')
run('qualified-clear',['logcat','-c']);start('qualified-start');time.sleep(14)
run('qualified-city-before',['exec-out','screencap','-p'],True);log('qualified-baseline')
w,h=struct.unpack('>II',(out/'qualified-city-before.png').read_bytes()[16:24])
run('qualified-yaw',['shell','input','swipe',str(int(w*.35)),str(int(h*.5)),str(int(w*.65)),str(int(h*.5)),'600']);time.sleep(3)
run('qualified-city-yaw',['exec-out','screencap','-p'],True)
run('qualified-zoom',['shell','input','swipe',str(w//2),str(int(h*.75)),str(w//2),str(int(h*.25)),'600']);time.sleep(3)
run('qualified-city-zoom',['exec-out','screencap','-p'],True);log('qualified-interaction')
for i in range(1,4):
 run(f'qualified-home-{i}',['shell','input','keyevent','3']);time.sleep(3)
 start(f'qualified-reopen-{i}');time.sleep(5);log(f'qualified-return-{i}')
run('qualified-portrait',['shell','wm','user-rotation','lock','0']);time.sleep(6)
run('qualified-city-portrait',['exec-out','screencap','-p'],True)
run('qualified-landscape',['shell','wm','user-rotation','lock','1']);time.sleep(6)
run('qualified-city-landscape',['exec-out','screencap','-p'],True);log('qualified-lifecycle')
for i in range(1,3):
 run(f'qualified-back-{i}',['shell','input','keyevent','4']);time.sleep(4);log(f'qualified-exit-{i}')
 start(f'qualified-launcher-{i}');time.sleep(10);log(f'qualified-relaunch-{i}')
# Select the old cube fixture explicitly after cleanup, then return to the city.
run('qualified-city-exit',['shell','input','keyevent','4']);time.sleep(4)
start('qualified-cube-start',['--ez','coinrender_city','false','--ez','coinrender_continuous','false']);time.sleep(5)
log('qualified-cube');run('qualified-cube-exit',['shell','input','keyevent','4']);time.sleep(3)
start('qualified-city-final');time.sleep(12)
log('qualified-final');run('qualified-city-final',['exec-out','screencap','-p'],True)
subprocess.run(['wmctrl','-a','Coin Render Android']);subprocess.run(['wmctrl','-a','Android Emulator - Medium_Phone_API_37.0:5554'])
