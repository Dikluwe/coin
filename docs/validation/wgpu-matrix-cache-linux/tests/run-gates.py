#!/usr/bin/env python3
"""Run selected CPU and GPU gates for the wgpu authored-matrix cache."""
import importlib.util,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
root=Path('/tmp/coin-render-first-frame')
s=importlib.util.spec_from_file_location('runner',root/'scripts/coinrender/run_animation_benchmark.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
out=Path('/tmp/coin-render-matrix-gates');out.mkdir(exist_ok=False)
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
cpu='^(CoinWgpuFfiFrameTest)$'
run('cpu',['ctest','--test-dir',str(wg),'-R',cpu,'--output-on-failure'],env,wg,cpu)
pattern='^(CoinWgpuMultiDeviceTest|CoinRenderCameraReuseReferenceTest|CoinRenderSceneTextureTest|CoinRenderSceneTextureDirectTest|CoinRenderTransparencyTest)$'
run('gpu-camera-rtt-transparency',['ctest','--test-dir',str(wg),'-R',pattern,'--output-on-failure'],env,wg,pattern)
run('gpu-composition',[str(wg/'bin/CoinRenderCompositionTest')],env)
run('gpu-multitexture',[str(wg/'bin/CoinRenderMultitextureTest')],env,required=('P08 Coin GL reference passed (CPU=0)',))
print('COMPLETE: repaired FFI oracle and 7 GPU executions; five CPU gates already passed in initial run',flush=True)
