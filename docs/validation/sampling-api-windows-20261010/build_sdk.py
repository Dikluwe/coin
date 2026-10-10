import json, os, pathlib, subprocess, time, argparse
p=argparse.ArgumentParser();p.add_argument('--tag',default='');p.add_argument('--backend',choices=['bgfx','wgpu','both'],default='both');a=p.parse_args()
ROOT = pathlib.Path(__file__).resolve().parent
SOURCE = pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
DEP = pathlib.Path(r'H:\Git\coin\build\bgfx-windows-install')
ENV = {k.upper():v for k,v in os.environ.items()}
ENV['MSBUILDDISABLENODEREUSE']='1'
ENV['CARGO_BUILD_JOBS']='2'
rows=[]
manifest_tag=a.tag or 'initial'
(ROOT/(manifest_tag+'-source.diff')).write_bytes(subprocess.check_output(['git','-C',str(SOURCE),'diff','--binary'],env=ENV))
(ROOT/(manifest_tag+'-source-sha.txt')).write_bytes(subprocess.check_output(['git','-C',str(SOURCE),'rev-parse','HEAD'],env=ENV))
def run(name,cmd):
    name=(a.tag+'-' if a.tag else '')+name
    start=time.time(); print(name,'started',flush=True)
    with (ROOT/(name+'.log')).open('w',encoding='utf-8') as log:
        code=subprocess.run(cmd,env=ENV,stdout=log,stderr=subprocess.STDOUT).returncode
    rows.append(dict(name=name,command=cmd,exit=code,seconds=time.time()-start))
    (ROOT/((a.tag+'-' if a.tag else '')+'build-ledger.json')).write_text(json.dumps(rows,indent=2),encoding='utf-8')
    print(name,'exit',code,'seconds',round(time.time()-start),flush=True)
    if code:
        print('\n'.join((ROOT/(name+'.log')).read_text(encoding='utf-8',errors='replace').splitlines()[-40:]),flush=True)
        raise SystemExit(code)
for backend,mode in [('bgfx','BGFX'),('wgpu','RUST_BRIDGE')]:
    if a.backend!='both' and backend!=a.backend:continue
    build=ROOT/('build-'+backend); prefix=ROOT/('install-'+backend)
    cmd=['cmake','-S',str(SOURCE),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64',
         '-DCOIN_BUILD_RENDER=ON','-DCOIN_BUILD_TESTS=ON','-DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON',
         '-DCOIN_BUILD_RENDER_BENCHMARKS=ON','-DCOIN_BUILD_LEGACY_GL_RENDERER=ON',
         '-DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON','-DCMAKE_INSTALL_PREFIX='+str(prefix),
         '-DCOIN_BUILD_MSVC_MP=OFF','-DCMAKE_CXX_FLAGS=/MP2',
         '-DCOIN_BUILD_AWESOME_DOCUMENTATION=OFF','-DCOIN_BUILD_DOCUMENTATION=OFF','-DCOIN_RENDER_BACKEND='+mode]
    if backend=='bgfx':
        cmd+=['-DCMAKE_PREFIX_PATH='+str(DEP),'-DCOIN_BGFX_SHADERC_EXECUTABLE='+str(DEP/'bin/shaderc.exe'),
              '-DCOIN_BGFX_SHADER_INCLUDE_DIR='+str(DEP/'include/bgfx')]
    run(backend+'-configure',cmd)
    run(backend+'-build',['cmake','--build',str(build),'--config','Release','--parallel','2'])
    run(backend+'-install',['cmake','--install',str(build),'--config','Release'])
    run(backend+'-consumer-configure',['cmake','-S',str(SOURCE/'examples/coinrender/sdk-consumer'),
         '-B',str(ROOT/('consumer-'+backend)),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_PREFIX_PATH='+str(prefix)])
    run(backend+'-consumer-build',['cmake','--build',str(ROOT/('consumer-'+backend)),'--config','Release','--parallel','2'])
