#!/usr/bin/env python3
"""Alternate frozen/current CoinRender with a shared Coin/OpenGL control."""
import json,subprocess,sys
from pathlib import Path
root=Path('/tmp/coin-render-first-frame')
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
common=['python3','/tmp/coin-render-all-matched.py','--before-bgfx-build','/tmp/coin-render-validation-baseline/bgfx','--after-bgfx-build','/tmp/coin-render-first-frame-bgfx','--before-wgpu-build','/tmp/coin-render-validation-baseline/wgpu','--after-wgpu-build','/tmp/coin-render-first-frame-wgpu','--coingl-build','/tmp/coin-render-material-geometry-baseline/coingl','--scene','/tmp/coin-render-city-40000.iv','--variants','coingl,bgfx-vulkan,bgfx-opengl,wgpu-vulkan','--rounds','3','--gpu','nvidia','--size','1024','--before-source-content-revision','ac28529a27da5ea370cf90eef4eeaf8b22f08850','--after-source-content-revision',revision,'--control-source-content-revision','4d63bb993022ee8d40802558b0871a4803002b8d','--runner',str(root/'scripts/coinrender/run_animation_benchmark.py'),'--row-helper','/tmp/coin-render-wgpu-motion-matched.py']
commands=[]
for name,cases,frames,warmup in [('offscreen','transforms-10,materials-10,geometry-10,static,camera','15','5'),('stress','geometry-100','7','3')]:
    cmd=common+['--warmup',warmup,'--scope','offscreen','--cases',cases,'--frames',frames,'--output-before','/tmp/coin-render-validation-'+name+'-before','--output-after','/tmp/coin-render-validation-'+name+'-after']
    commands.append({'name':name,'command':cmd,'source_content_revision':revision})
    Path('/tmp/coin-render-validation-campaign-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print('CAMPAIGN',name,flush=True)
    with Path('/tmp/coin-render-validation-'+name+'-run.log').open('w') as log:
        process=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,text=True)
    commands[-1]['exit_code']=process.returncode
    Path('/tmp/coin-render-validation-campaign-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    if process.returncode:raise SystemExit(process.returncode)
    print('FINISHED',name,flush=True)
