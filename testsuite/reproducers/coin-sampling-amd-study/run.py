#!/usr/bin/env python3
"""Coin-master and Coin-free EGL controls; always preserve commands and exits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--build',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
summary={'runs':[]}
for gpu in ['amd','nvidia']:
    env=dict(os.environ,DISPLAY=':0',XAUTHORITY=str(Path.home()/'.Xauthority'),
             COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_GLXGLUE_NO_PBUFFERS='1')
    env['__GLX_VENDOR_LIBRARY_NAME']='mesa' if gpu=='amd' else 'nvidia'
    env['__EGL_VENDOR_LIBRARY_FILENAMES']='/usr/share/glvnd/egl_vendor.d/'+('50_mesa.json' if gpu=='amd' else '10_nvidia.json')
    if gpu=='nvidia':env['__NV_PRIME_RENDER_OFFLOAD']='1'
    else:env.pop('__NV_PRIME_RENDER_OFFLOAD',None)
    for quality in ['.5','.8']:
        for offset in ['0','-.001','.001']:
            for api,binary in [('coin','coin-native-sampling'),('egl','pure-egl-sampling')]:
                name=f'{gpu}-{api}-q{quality}-o{offset}'
                cmd=[str(args.build/binary),str(args.output/name),quality,offset]
                if api=='coin' and gpu=='nvidia':cmd.append('--egl-current')
                log=args.output/(name+'.log')
                try:
                    with log.open('w') as f:r=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=90)
                    code=r.returncode
                except subprocess.TimeoutExpired:code=124
                summary['runs'].append(dict(gpu=gpu,api=api,quality=float(quality),offset=float(offset),
                    command=cmd,environment={k:v for k,v in env.items() if k in ['DISPLAY','XAUTHORITY','COIN_GLX_PIXMAP_DIRECT_RENDERING','COIN_GLXGLUE_NO_PBUFFERS','__GLX_VENDOR_LIBRARY_NAME','__EGL_VENDOR_LIBRARY_FILENAMES','__NV_PRIME_RENDER_OFFLOAD']},
                    exit=code,binary_sha256=hashlib.sha256((args.build/binary).read_bytes()).hexdigest(),
                    log=log.name,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
                    renderer=[x for x in log.read_text().splitlines() if x.startswith('route=')]))
                (args.output/'runs.json').write_text(json.dumps(summary,indent=2)+'\n')
                print(name,code,flush=True)
raise SystemExit(0 if all(r['exit']==0 for r in summary['runs']) else 1)
