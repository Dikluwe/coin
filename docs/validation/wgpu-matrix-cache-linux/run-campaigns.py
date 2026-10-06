#!/usr/bin/env python3
"""Alternate frozen/current wgpu with one shared Coin/OpenGL control."""
import json,subprocess
from pathlib import Path
root=Path('/tmp/coin-render-first-frame')
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
common=['python3','/tmp/coin-render-wgpu-motion-matched.py','--bgfx-build','/tmp/coin-render-material-geometry-baseline/coingl','--before-wgpu-build','/tmp/coin-render-matrix-baseline/wgpu','--after-wgpu-build','/tmp/coin-render-first-frame-wgpu','--scene','/tmp/coin-render-city-40000.iv','--scope','offscreen','--rounds','3','--gpu','nvidia','--size','1024','--before-source-content-revision','6182410f5789bbdc30d5ccd06fa341e1810aef8e','--after-source-content-revision',revision,'--control-source-content-revision','4d63bb993022ee8d40802558b0871a4803002b8d','--runner',str(root/'scripts/coinrender/run_animation_benchmark.py')]
commands=[]
for name,cases,frames,warmup in [('offscreen','transforms-10,materials-10,geometry-10,static,camera','15','5'),('stress','transforms-100,geometry-100','7','3')]:
    cmd=common+['--warmup',warmup,'--cases',cases,'--frames',frames,'--output-before','/tmp/coin-render-matrix-'+name+'-before','--output-after','/tmp/coin-render-matrix-'+name+'-after']
    commands.append({'name':name,'command':cmd,'source_content_revision':revision})
    record=Path('/tmp/coin-render-matrix-campaign-commands.json');record.write_text(json.dumps(commands,indent=2)+'\n')
    print('CAMPAIGN',name,flush=True)
    with Path('/tmp/coin-render-matrix-'+name+'-run.log').open('w') as log:
        p=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,text=True)
    commands[-1]['exit_code']=p.returncode;record.write_text(json.dumps(commands,indent=2)+'\n')
    if p.returncode:raise SystemExit(p.returncode)
    print('FINISHED',name,flush=True)
