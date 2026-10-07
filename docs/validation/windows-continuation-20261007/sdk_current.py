import argparse, hashlib, json, os, pathlib, subprocess, time
parser=argparse.ArgumentParser()
parser.add_argument('--phase',choices=['all','prepare','probe'],default='all')
args=parser.parse_args()
ROOT=pathlib.Path(__file__).resolve().parent
BASE={k.upper():v for k,v in os.environ.items() if not k.upper().startswith(('COIN_','WGPU_'))}
BASE['MSBUILDDISABLENODEREUSE']='1'
records=json.loads((ROOT/'sdk-results.json').read_text()) if (ROOT/'sdk-results.json').exists() else []
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def run(label, command, env=BASE):
    start=time.time()
    with (ROOT/(label+'.log')).open('w',encoding='utf-8') as log:
        proc=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
    record=dict(label=label,command=command,exit=proc.returncode,seconds=round(time.time()-start,3))
    records.append(record)
    (ROOT/'sdk-results.json').write_text(json.dumps(records,indent=2))
    print(time.strftime('%H:%M:%S'),label,'exit',proc.returncode,flush=True)
    if proc.returncode:
        print((ROOT/(label+'.log')).read_text(errors='replace')[-3500:],flush=True)
        raise SystemExit(proc.returncode)
for backend in ['bgfx','wgpu']:
    build=ROOT/backend
    prefix=ROOT/('install-'+backend)
    consumer=ROOT/('sdk-consumer-'+backend)
    if args.phase in ['all','prepare']:
        before={name:digest(build/'bin'/name) for name in ['Coin4.dll','CoinRender4.dll']}
        run(backend+'-sdk-configure',['cmake','-S',str(pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\coin-render-windows-20261007\coin')),
            '-B',str(build),'-DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON'])
        run(backend+'-sdk-install',['cmake','--install',str(build),'--config','Release','--prefix',str(prefix)])
        for name in before:
            assert before[name]==digest(build/'bin'/name)==digest(prefix/'bin'/name), 'Installed DLL differs from tested binary'
        (ROOT/(backend+'-sdk-dll-hashes.json')).write_text(json.dumps(before,indent=2))
        run(backend+'-consumer-configure',['cmake','-S',str(ROOT/'sdk-consumer'),'-B',str(consumer),
            '-G','Visual Studio 17 2022','-A','x64',
            '-DCMAKE_PREFIX_PATH='+str(prefix)+';H:/Git/coin/build/bgfx-windows-install'])
        run(backend+'-consumer-build',['cmake','--build',str(consumer),'--config','Release','--parallel','2'])
    if args.phase=='prepare': continue
    exe=next(consumer.rglob('coin-render-sdk-consumer.exe'))
    for api in ['d3d12','vulkan','opengl']:
        env=dict(BASE)
        env['PATH']=str(prefix/'bin')+os.pathsep+env['PATH']
        env['COIN_BGFX_RENDERER' if backend=='bgfx' else 'WGPU_BACKEND']=api if backend=='bgfx' else {'d3d12':'dx12','vulkan':'vulkan','opengl':'gl'}[api]
        run(backend+'-sdk-'+api,[str(exe),api],env)
