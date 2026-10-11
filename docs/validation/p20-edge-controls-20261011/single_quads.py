import json
import os
from pathlib import Path
import re
import subprocess
import sys

if len(sys.argv) != 4:
    raise SystemExit('usage: single_quads.py TRANSPARENT_SCENE OUT BGFX_BUILD')
source = Path(sys.argv[1]).read_text()
out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
quads = re.findall(r'Separator \{\n  Material \{.*?\n\}', source, flags=re.S)
assert len(quads) == 49, len(quads)
points = [(36,32), (82,65), (80,79), (59,87)]
rows = []
for q in range(1, 49):
    active = []
    for i, quad in enumerate(quads):
        active.append(quad if i == 0 or i == q else
                      re.sub(r'transparency [0-9.]+', 'transparency 1', quad))
    scene = out / f'quad-{q:02d}.iv'
    scene.write_text('#Inventor V2.1 ascii\nSeparator {\n' + '\n'.join(active) + '\n}\n')
    images = {}
    for backend, build in [('gl',sys.argv[3]), ('bgfx',sys.argv[3])]:
        image = out / f'quad-{q:02d}-{backend}.ppm'
        env = dict(os.environ,
                   LD_LIBRARY_PATH=build+'/lib:'+os.environ.get('LD_LIBRARY_PATH',''),
                   COIN_GLX_PIXMAP_DIRECT_RENDERING='1', COIN_GLXGLUE_NO_PBUFFERS='1',
                   COIN_BGFX_RENDERER='vulkan')
        cmd = [build+'/bin/coin_render_gl_benchmark', '--backend', backend,
               '--scene', str(scene), '--warmup', '1', '--frames', '1',
               '--size', '128', '--image-output', str(image)]
        p = subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=60)
        if p.returncode: raise RuntimeError(f'quad {q} {backend}: '+p.stdout+p.stderr)
        images[backend] = image.read_bytes().split(b'\n',3)[-1]
    gl,vk=images['gl'],images['bgfx']
    selected=[]
    for x,y in points:
        i=(y*128+x)*3
        a=list(gl[i:i+3]);b=list(vk[i:i+3])
        if max(abs(a[c]-b[c]) for c in range(3))>3:
            selected.append({'xy':[x,y], 'gl':a,'vulkan':b})
    if selected: print('quad',q,selected,flush=True)
    rows.append({'quad':q,'pixels':selected})
    (out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
