import hashlib,json,pathlib,subprocess,sys,time
sys.dont_write_bytecode=True
ROOT=pathlib.Path('/tmp/coin-render-first-frame')
OUT=pathlib.Path('/tmp/coin-uv-public-abi-validation')
sys.path.insert(0,str(ROOT/'scripts/coinrender'))
from run_animation_benchmark import environment
builds={'wgpu-vulkan':pathlib.Path('/tmp/coin-uv-abi-candidate-build'),'bgfx-vulkan':pathlib.Path('/tmp/coin-render-first-frame-bgfx'),'bgfx-opengl':pathlib.Path('/tmp/coin-render-first-frame-bgfx')}
def run(name,variant,command):
 build=builds[variant];env=environment(build,variant,'nvidia');env['COIN_GLX_PIXMAP_DIRECT_RENDERING']='1';env['COIN_RENDER_REQUIRE_GL_REFERENCE']='1'
 env['COIN_BGFX_TRANSPARENCY']='object'
 binaries=[build/'lib/libCoin.so',build/'lib/libCoinRender.so']
 if pathlib.Path(command[0]).is_file():binaries.append(pathlib.Path(command[0]))
 else:
  selected=json.loads(subprocess.check_output(command[:1]+['--show-only=json-v1']+command[1:],text=True))
  binaries.extend(pathlib.Path(test['command'][0]) for test in selected['tests'])
 hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in binaries}
 log=OUT/(name+'-'+variant+'.log');start=time.monotonic()
 with log.open('w') as stream:result=subprocess.run(command,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT)
 record={'phase':name,'variant':variant,'command':command,'exit_code':result.returncode,'seconds':round(time.monotonic()-start,3),'log':log.name,'executed_binaries_sha256':hashes,'environment':{k:env.get(k) for k in ['LD_LIBRARY_PATH','WGPU_BACKEND','VK_ICD_FILENAMES','COIN_GLX_PIXMAP_DIRECT_RENDERING','COIN_RENDER_REQUIRE_GL_REFERENCE','COIN_BGFX_RENDERER','COIN_BGFX_TRANSPARENCY','__NV_PRIME_RENDER_OFFLOAD','__GLX_VENDOR_LIBRARY_NAME']}}
 (OUT/(name+'-'+variant+'.json')).write_text(json.dumps(record,indent=2)+'\n')
 print(json.dumps({k:v for k,v in record.items() if k!='executed_binaries_sha256'}),flush=True)
 print('\n'.join(log.read_text().splitlines()[-4:] if result.returncode==0 else log.read_text().splitlines()[-25:]),flush=True)
 if result.returncode:sys.exit(result.returncode)
run('core','wgpu-vulkan',['ctest','--test-dir',str(builds['wgpu-vulkan']),'-R','^(CoinTests|CoinConfigCompatibility|RenderManagerFramePreparationTest|CoinRenderActionTest|CoinRenderProjectiveUvCaptureTest|CoinRenderBoundingBoxCaptureTest)$','--output-on-failure','-j','1'])
run('capture','bgfx-vulkan',[str(builds['bgfx-vulkan']/'bin/CoinRenderProjectiveUvTest'),'--capture'])
for variant,build in builds.items():run('uv',variant,[str(build/'bin/CoinRenderProjectiveUvTest'),'--gpu'])
