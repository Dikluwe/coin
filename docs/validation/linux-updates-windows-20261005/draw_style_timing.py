import json
import os
from pathlib import Path
import subprocess
import time

root=Path('H:/Git/coin')
out=root/'build/linux-updates-windows-20261005'
env=os.environ.copy()
env['WGPU_BACKEND']='gl'
env['COIN_RENDER_REQUIRE_GL_REFERENCE']='1'
env['COIN_WGPU_REQUIRE_GL_REFERENCE']='1'
exe=root/'build/coin-render-wgpu-msvc/bin/CoinRenderDrawStyleTest.exe'
start=time.perf_counter()
with (out/'draw-style-gl-extended.log').open('wb') as stream:
    result=subprocess.run([str(exe)],cwd=root,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=600)
record=dict(exit_code=result.returncode,seconds=time.perf_counter()-start,backend='wgpu',api='gl',timeout=600,mandatory_gl_reference=True)
(out/'draw-style-gl-timing.json').write_text(json.dumps(record,indent=2)+'\n')
print(record,flush=True)
raise SystemExit(result.returncode)
