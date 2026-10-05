#!/usr/bin/env python3
"""Run selected Core and GPU gates for the local combine-validation memo."""
import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-validation-gates');out.mkdir(exist_ok=False)
wg=Path('/tmp/coin-render-first-frame-wgpu');bg=Path('/tmp/coin-render-first-frame-bgfx')
meta={'source_content_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'commands':[]}
def run(name,cmd,env,build=None,pattern=None,required=()):
    record={'name':name,'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('COIN_','WGPU_','__NV','__GLX','VK_','LD_LIBRARY'))},'required_output':list(required)}
    if pattern:record['ctest_definitions']=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'-R',pattern,'--show-only=json-v1'],text=True))['tests']
    meta['commands'].append(record);(out/'commands.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('START',name,flush=True)
    p=subprocess.run(cmd,cwd=root,env=env,capture_output=True,text=True,timeout=300)
    output=p.stdout+p.stderr;(out/(name+'.log')).write_text(output)
    record['exit_code']=p.returncode
    record['required_output_observed']=all(x in output for x in required)
    record['skip_observed']='[SKIP]' in output or 'Skipped' in output
    (out/'commands.json').write_text(json.dumps(meta,indent=2)+'\n')
    print('END',name,p.returncode,flush=True)
    if p.returncode or not record['required_output_observed'] or record['skip_observed']:
        print(output[-5000:]);raise SystemExit(p.returncode or 1)
    if pattern:
        assert '100% tests passed' in output
        (out/(name+'-ctest.log')).write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
env=r.environment(wg,'wgpu-vulkan','nvidia')
env.update(COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
cpu='^(CoinRenderFrameCoreTest|CoinRenderActionTest|CoinRenderPlanAssemblyCoreTest|CoinRenderFrameReuseCoreTest|CoinRenderTransformCoreTest|CoinWgpuFfiFrameTest|CoinBgfxCoreTest)$'
run('cpu',['ctest','--test-dir',str(wg),'-R',cpu,'--output-on-failure'],env,wg,cpu)
run('composition-cpu',[str(wg/'bin/CoinRenderCompositionTest'),'--range-memo'],env)
run('bgfx-frame-core',[str(bg/'bin/CoinRenderFrameCoreTest')],r.environment(bg,'bgfx-vulkan','nvidia'))
for variant in ('wgpu-vulkan','bgfx-vulkan','bgfx-opengl'):
    build=wg if variant.startswith('wgpu') else bg
    env=r.environment(build,variant,'nvidia');env.update(COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
    for test in ('CoinRenderTextureTest','CoinRenderMultitextureTest','CoinRenderCompositionTest'):
        required=('P08 Coin GL reference passed (CPU=0)',) if test=='CoinRenderMultitextureTest' else ()
        run(variant+'-'+test,[str(build/'bin'/test)],env,required=required)
env=r.environment(wg,'wgpu-vulkan','nvidia');env.update(COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
pattern='^(CoinRenderCameraReuseReferenceTest|CoinRenderSceneTextureDirectTest)$'
run('wgpu-camera-rtt',['ctest','--test-dir',str(wg),'-R',pattern,'--output-on-failure'],env,wg,pattern)
run('clipping-reference',[str(wg/'bin/CoinRenderClipPlaneTest')],env,required=('Coin/GL reference passed',))
env=r.environment(bg,'bgfx-vulkan','nvidia');env.update(COIN_RENDER_REQUIRE_GL_REFERENCE='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1')
pattern='^(CoinRenderRttOwnership_vulkan|CoinRenderRttOwnership_opengl)$'
run('bgfx-rtt',['ctest','--test-dir',str(bg),'-R',pattern,'--output-on-failure'],env,bg,pattern)
print('COMPLETE: 9 Core/CPU executions and 14 GPU executions',flush=True)
