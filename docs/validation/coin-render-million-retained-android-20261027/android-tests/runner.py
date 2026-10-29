from pathlib import Path
import subprocess,json
out=Path('/tmp/coin-retained-million-final-tests');out.mkdir(exist_ok=True)
adb='/home/dikluwe/Android/Sdk/platform-tools/adb';remote='/data/local/tmp/coin-retained-million-final';build=Path('/tmp/coin-retained-android-wgpu');calls=[]
def run(label,args):
 r=subprocess.run([adb,'-s','emulator-5580']+args,capture_output=True,text=True,timeout=90)
 (out/(label+'.log')).write_text(r.stdout+r.stderr);calls.append(dict(label=label,args=args,exit=r.returncode));(out/'commands.json').write_text(json.dumps(calls,indent=2)+'\n')
 print(label,r.returncode,(r.stdout+r.stderr)[-1600:],flush=True);assert r.returncode==0
run('mkdir',['shell','mkdir','-p',remote])
for path in [build/'lib/libCoin.so',build/'lib/libCoinRender.so',Path('/home/dikluwe/Android/Sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so')]+[build/'bin'/name for name in ['CoinRenderRetainedCityTest','CoinRenderRetainedIncrementalTest','CoinRenderRetainedConnectorTest']]:run('push-'+path.name,['push',str(path),remote+'/'])
for name,extra in [('CoinRenderRetainedCityTest',[]),('CoinRenderRetainedIncrementalTest',[]),('CoinRenderRetainedConnectorTest',[]),('CoinRenderRetainedConnectorTest',['COIN_RENDER_DISABLE_COMPOSITION_BORROW=1'])]:
 run(name+('-no-borrow' if extra else ''),['shell','env','LD_LIBRARY_PATH='+remote,'COIN_RETAINED_TEST_RENDERER=opengl']+extra+[remote+'/'+name])
