import json
import os
from pathlib import Path
import re
import subprocess
import sys

if len(sys.argv) != 5:
    raise SystemExit('usage: variants.py SCENES_DIR OUT BGFX_BUILD WGPU_BUILD')
scene_dir = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
base = (scene_dir / 'transparent-overlap.iv').read_text()
variants = {
    'original': base,
    'opaque': re.sub(r'transparency [0-9.]+', 'transparency 0', base),
    'uniform-half': re.sub(r'transparency [0-9.]+', 'transparency 0.5', base),
}
builds = {'gl': sys.argv[3], 'bgfx-opengl': sys.argv[3],
          'bgfx-vulkan': sys.argv[3],
          'wgpu-vulkan': sys.argv[4]}
images = {}
results = []
for name, content in variants.items():
    scene = out / (name + '.iv')
    scene.write_text(content)
    for backend, build in builds.items():
        target = out / (name + '-' + backend + '.ppm')
        env = dict(os.environ,
                   LD_LIBRARY_PATH=build + '/lib:' + os.environ.get('LD_LIBRARY_PATH', ''),
                   COIN_GLX_PIXMAP_DIRECT_RENDERING='1',
                   COIN_GLXGLUE_NO_PBUFFERS='1',
                   COIN_BGFX_TRACE_GL_ADAPTER='1',
                   COIN_BGFX_RENDERER='opengl' if backend == 'bgfx-opengl' else 'vulkan')
        cmd = [build + '/bin/coin_render_gl_benchmark', '--backend',
               'gl' if backend == 'gl' else 'bgfx' if backend.startswith('bgfx') else 'wgpu',
               '--scene', str(scene), '--warmup', '1', '--frames', '3',
               '--size', '128', '--image-output', str(target)]
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=60)
        (out / (name + '-' + backend + '.log')).write_text(proc.stdout + proc.stderr)
        if proc.returncode:
            raise RuntimeError(name + '/' + backend + ': ' + str(proc.returncode) + '\n' + (proc.stdout + proc.stderr)[-1200:])
        raw = target.read_bytes().split(b'\n', 3)[-1]
        if len(raw) != 128*128*3:
            raise RuntimeError('wrong image extent ' + str(target))
        images[name, backend] = raw
        print(name, backend, len(raw), flush=True)
    gl, bgfx_gl, bgfx, wgpu = (images[name, key] for key in
                              ('gl', 'bgfx-opengl', 'bgfx-vulkan', 'wgpu-vulkan'))
    pixels = []
    for y in range(128):
        for x in range(128):
            i = (y*128 + x)*3
            if max(abs(gl[i+c]-bgfx[i+c]) for c in range(3)) > 3:
                pixels.append({'xy': [x,y], 'gl': list(gl[i:i+3]),
                               'vulkan': list(bgfx[i:i+3])})
    gl_over3 = sum(max(abs(gl[i+c]-bgfx_gl[i+c]) for c in range(3)) > 3
                   for i in range(0,len(gl),3))
    row = {'variant': name, 'pixels_over3': len(pixels),
           'bgfx_opengl_pixels_over3': gl_over3,
           'vulkan_backends_equal': bgfx == wgpu, 'pixels': pixels}
    results.append(row)
    (out / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    print('summary', name, len(pixels), 'bgfx_gl_over3', gl_over3,
          'vulkan_pair', bgfx == wgpu, flush=True)
