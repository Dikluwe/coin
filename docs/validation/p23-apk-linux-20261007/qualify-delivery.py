from pathlib import Path
import subprocess,time,json,re
root=Path(__file__).resolve().parent
adb='/home/dikluwe/Android/Sdk/platform-tools/adb';pkg='org.coin3d.coinrender.p23'
commands=[]
def run(*args):
    argv=[adb,'-s','emulator-5554',*args]
    r=subprocess.run(argv,capture_output=True,text=True,timeout=25)
    commands.append({'command':argv,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
    if r.returncode:raise RuntimeError(r.stderr)
    return r.stdout
try:
    run('install','--no-incremental','-r',str(root/'apk-delivery-x86_64/coin-render-p23-x86_64.apk'))
    run('shell','am','force-stop',pkg)
    for _ in range(30):
        r=subprocess.run([adb,'-s','emulator-5554','shell','pidof',pkg],capture_output=True,text=True)
        if not r.stdout.strip():break
        time.sleep(.2)
    else:raise RuntimeError('Previous process active')
    run('shell','am','start','-W','-f','0x10008000','-n',pkg+'/android.app.NativeActivity')
    pid=run('shell','pidof',pkg).strip()
    def observe(predicate):
        for _ in range(50):
            text=run('logcat','-d','--pid='+pid)
            frames=re.findall(r'generation=(\d+) serial=(\d+) size=(\d+)x(\d+) rgba_fnv64=([0-9a-f]+) capture_scope=(\w+)',text)
            assert 'requested_renderer=2' in text and 'legacy_gl_unavailable=1' in text
            assert 'E CoinRenderP23' not in text and 'Fatal signal' not in text
            assert all(f[4]=='dc8224a1b44d3d8d' and f[5]=='offscreen' for f in frames)
            if predicate(text,frames):return text,frames
            time.sleep(.2)
        raise RuntimeError('Expected state not observed')
    text,frames=observe(lambda t,f:len(f)>=2)
    count=len(frames)
    (root/'delivery-default.log').write_text(text)
    run('shell','input','keyevent','KEYCODE_HOME');time.sleep(.5)
    run('shell','am','start','-W','-n',pkg+'/android.app.NativeActivity')
    text,frames=observe(lambda t,f:len(f)>count)
    old=run('shell','wm','user-rotation').strip()
    try:
        run('shell','wm','user-rotation','lock','1')
        text,frames=observe(lambda t,f:any(int(v[2])>int(v[3]) for v in f))
        png=subprocess.check_output([adb,'-s','emulator-5554','exec-out','screencap','-p'],timeout=20)
        (root/'delivery-landscape.png').write_bytes(png)
    finally:
        if old=='free':run('shell','wm','user-rotation','free')
        elif old.startswith('lock '):run('shell','wm','user-rotation','lock',old.split()[-1])
        else:raise RuntimeError('Unknown original rotation mode')
    count=len(frames)
    text,frames=observe(lambda t,f:len(f)>count and int(f[-1][2])<int(f[-1][3]))
    run('shell','input','keyevent','KEYCODE_BACK')
    text,frames=observe(lambda t,f:'P23 smoke finished: OK' in t)
    assert 'P23 smoke finished: FAILED' not in text
    (root/'delivery-lifecycle.log').write_text(text)
    (root/'delivery-result.json').write_text(json.dumps({'pid':pid,'frames':frames,'original_rotation_restored':old,'result':'passed'},indent=2)+'\n')
    print(json.dumps({'pid':pid,'checkpoints':len(frames),'serials':[int(f[1]) for f in frames],'result':'passed'}))
finally:
    (root/'delivery-commands-raw.json').write_text(json.dumps(commands,indent=2)+'\n')
