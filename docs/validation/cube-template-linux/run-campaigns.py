import subprocess, sys
from pathlib import Path
root=Path('/tmp/coin-render-first-frame')
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
common=['python3','/tmp/coin-render-all-matched.py','--before-bgfx-build','/tmp/coin-render-source-sharing-baseline/bgfx','--after-bgfx-build','/tmp/coin-render-first-frame-bgfx','--before-wgpu-build','/tmp/coin-render-source-sharing-baseline/wgpu','--after-wgpu-build','/tmp/coin-render-first-frame-wgpu','--coingl-build','/tmp/coin-render-material-geometry-baseline/coingl','--scene','/tmp/coin-render-city-40000.iv','--variants','coingl,bgfx-vulkan,bgfx-opengl,wgpu-vulkan','--rounds','3','--warmup','3','--gpu','nvidia','--size','1024','--before-source-content-revision','ff289a50b971fbc012e791194acf0a5ec24cfb3b','--after-source-content-revision',revision,'--control-source-content-revision','4d63bb993022ee8d40802558b0871a4803002b8d','--runner',str(root/'scripts/coinrender/run_animation_benchmark.py'),'--row-helper','/tmp/coin-render-wgpu-motion-matched.py']
for name,scope,cases,frames,warmup in [('offscreen','offscreen','transforms-10,materials-10,geometry-10,static,camera','15','5'),('stress','offscreen','geometry-100','7','3')]:
 cmd=common+['--warmup',warmup,'--scope',scope,'--cases',cases,'--frames',frames,'--output-before','/tmp/coin-render-cube-'+name+'-before','--output-after','/tmp/coin-render-cube-'+name+'-after']
 print('CAMPAIGN',name,flush=True)
 with Path('/tmp/coin-render-cube-'+name+'-run.log').open('w') as log:
  result=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,text=True)
 if result.returncode:sys.exit(result.returncode)
 print('FINISHED',name,flush=True)
