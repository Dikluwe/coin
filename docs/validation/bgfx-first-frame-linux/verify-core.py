import subprocess
from pathlib import Path
out=Path('/tmp/coin-render-bgfx-300-validation');out.mkdir(exist_ok=True)
for name,build,tests in [('bgfx','/tmp/coin-render-first-frame-bgfx','CoinRenderActionTest|CoinRenderPlanAssemblyCoreTest|CoinRenderFrameCoreTest|CoinRenderIndexedGeometryCoreTest|CoinBgfxCoreTest|CoinBgfxProgramSelectionTest'),('wgpu','/tmp/coin-render-first-frame-wgpu','CoinRenderActionTest|CoinRenderPlanAssemblyCoreTest|CoinRenderFrameCoreTest|CoinRenderIndexedGeometryCoreTest|CoinWgpuFfiFrameTest'),('recording','/tmp/coin-render-isolation-recording','CoinRenderActionTest|CoinRenderPlanAssemblyCoreTest|CoinRenderFrameCoreTest')]:
 p=subprocess.run(['ctest','--test-dir',build,'-R','^('+tests+')$','--output-on-failure'],capture_output=True,text=True)
 (out/(name+'-core-tests.log')).write_text(p.stdout+p.stderr);print(p.stdout,flush=True)
 assert p.returncode==0
