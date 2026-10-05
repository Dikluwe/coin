import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame');s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-capture-gates');out.mkdir(exist_ok=False)
meta={'source_content_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'commands':[]}
def run(name,build,variant,pattern):
 env=r.environment(build,variant,'nvidia');env.update(COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
 command=['ctest','--test-dir',str(build),'-R',pattern,'--output-on-failure']
 item={'name':name,'command':command,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_','__NV','__GLX','VK_','LD_LIBRARY'))},'ctest_definitions':json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'-R',pattern,'--show-only=json-v1'],text=True))['tests']}
 assert item['ctest_definitions'],name
 meta['commands'].append(item);(out/'commands.json').write_text(json.dumps(meta,indent=2)+'\n');print('START',name,flush=True)
 p=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True,timeout=360);log=p.stdout+p.stderr;(out/(name+'.log')).write_text(log)
 item.update(exit_code=p.returncode,skip_observed='[SKIP]' in log or 'Skipped' in log)
 (out/'commands.json').write_text(json.dumps(meta,indent=2)+'\n')
 (out/(name+'-ctest.log')).write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
 print('END',name,p.returncode,flush=True)
 if p.returncode or item['skip_observed'] or '100% tests passed' not in log:raise RuntimeError(name+' failed\n'+log[-4000:])
wg=Path('/tmp/coin-render-first-frame-wgpu');bg=Path('/tmp/coin-render-first-frame-bgfx')
run('core',wg,'wgpu-vulkan','^(CoinRenderFrameCoreTest|CoinRenderPlanAssemblyCoreTest)$')
run('wgpu-action-reuse',wg,'wgpu-vulkan','^(CoinRenderActionTest|CoinRenderFrameReuseCoreTest)$')
run('bgfx-action-reuse',bg,'bgfx-vulkan','^(CoinRenderActionTest|CoinRenderFrameReuseCoreTest)$')
for variant in ('wgpu-vulkan','bgfx-vulkan','bgfx-opengl'):
 build=wg if variant.startswith('wgpu') else bg
 run(variant+'-camera-transparency',build,variant,'^(CoinRenderCameraReuseReferenceTest|CoinRenderTransparencyTest)$')
run('wgpu-rtt-runtime',wg,'wgpu-vulkan','^(CoinRenderSceneTextureTest|CoinRenderSceneTextureDirectTest|CoinWgpuMultiDeviceTest|CoinRenderAsyncActionTest)$')
run('bgfx-rtt',bg,'bgfx-vulkan','^(CoinRenderRttOwnership_vulkan|CoinRenderRttOwnership_opengl)$')
print('COMPLETE: 2 pure Core, 4 Action/Reuse integration and 12 GPU executions',flush=True)
