import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-state-gates');out.mkdir(exist_ok=False)
wg=Path('/tmp/coin-render-first-frame-wgpu');bg=Path('/tmp/coin-render-first-frame-bgfx')
cpu='^(CoinRenderActionTest|CoinRenderPlanAssemblyCoreTest|CoinRenderFrameReuseCoreTest|CoinRenderTransformCoreTest|CoinWgpuFfiFrameTest|CoinBgfxCoreTest|CoinRenderDepthContractTest|CoinRenderDrawStyleTest|CoinRenderClipPlaneTest)$'
wg_gpu='^(CoinWgpuMultiDeviceTest|CoinRenderCameraReuseReferenceTest|CoinRenderSceneTextureDirectTest)$'
bg_gpu='^(CoinBgfxInstancingTest|CoinBgfxInstancingOpenGLTest|CoinRenderDepthContractVulkanTest|CoinRenderDepthContractOpenGLTest|CoinRenderRttOwnership_vulkan|CoinRenderRttOwnership_opengl)$'
meta={'commands':[]}
def run(name,cmd,env,build=None,ctest_pattern=None):
 record={'name':name,'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_','__NV','__GLX','VK_','LD_LIBRARY'))}}
 if ctest_pattern:
  record['ctest_definitions']=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'-R',ctest_pattern,'--show-only=json-v1'],text=True))['tests']
 meta['commands'].append(record);(out/'commands.json').write_text(json.dumps(meta,indent=2)+'\n')
 print('START',name,flush=True)
 process=subprocess.run(cmd,cwd=root,env=env,capture_output=True,text=True,timeout=300)
 (out/(name+'.log')).write_text(process.stdout+process.stderr)
 record['exit_code']=process.returncode
 (out/'commands.json').write_text(json.dumps(meta,indent=2)+'\n')
 print('END',name,process.returncode,flush=True)
 if process.returncode:print(process.stdout[-4000:]+process.stderr[-2000:]);raise SystemExit(process.returncode)
 if ctest_pattern:
  assert '100% tests passed' in process.stdout and 'Skipped' not in process.stdout,process.stdout
  (out/(name+'-ctest.log')).write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
env=r.environment(wg,'wgpu-vulkan','nvidia')
run('cpu',['ctest','--test-dir',str(wg),'-R',cpu,'--output-on-failure'],env,wg,cpu)
run('composition-cpu',[str(wg/'bin/CoinRenderCompositionTest'),'--range-memo'],env)
env.update(COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1',COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
run('wgpu',['ctest','--test-dir',str(wg),'-R',wg_gpu,'--output-on-failure'],env,wg,wg_gpu)
run('surface',[str(wg/'bin/CoinRenderSurfaceTest'),'--camera-only','--require-display'],env)
run('wgpu-composition',[str(wg/'bin/CoinRenderCompositionTest')],env)
env=r.environment(bg,'bgfx-vulkan','nvidia')
run('bgfx',['ctest','--test-dir',str(bg),'-R',bg_gpu,'--output-on-failure'],env,bg,bg_gpu)
for variant in ('bgfx-vulkan','bgfx-opengl'):
 run(variant+'-composition',[str(bg/'bin/CoinRenderCompositionTest')],r.environment(bg,variant,'nvidia'))
print('COMPLETE: 10 CPU and 13 GPU gates',flush=True)
