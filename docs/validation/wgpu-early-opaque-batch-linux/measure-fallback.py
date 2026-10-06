import json, os, re, subprocess, hashlib
from pathlib import Path
out = Path('/tmp/coin-render-early-batch-fallback-results')
out.mkdir(exist_ok=True)
variants = [('bgfx-vulkan', 'bgfx', 'nvidia', 'vulkan'),
            ('bgfx-opengl', 'bgfx', 'nvidia', 'opengl'),
            ('wgpu-vulkan', 'wgpu', 'nvidia', 'vulkan'),
            ('wgpu-opengl', 'wgpu', 'amd', 'gl')]
rows = []
for variant, backend, gpu, api in variants[2:3]:
    env = os.environ.copy()
    for name in ['COIN_RENDER_TRACE_PHASES', '__NV_PRIME_RENDER_OFFLOAD',
                 '__GLX_VENDOR_LIBRARY_NAME', '__EGL_VENDOR_LIBRARY_FILENAMES',
                 'COIN_BGFX_RENDERER', 'WGPU_BACKEND']:
        env.pop(name, None)
    if gpu == 'nvidia':
        env.update(__NV_PRIME_RENDER_OFFLOAD='1', __GLX_VENDOR_LIBRARY_NAME='nvidia')
        env['VK_ICD_FILENAMES'] = '/usr/share/vulkan/icd.d/nvidia_icd.json'
    else:
        env['VK_ICD_FILENAMES'] = '/usr/share/vulkan/icd.d/radeon_icd.json'
    env['COIN_BGFX_RENDERER' if backend == 'bgfx' else 'WGPU_BACKEND'] = api
    for pair in range(1, 4):
        for version in ['before', 'after']:
            build = (f'/tmp/coin-render-optimization-baseline-{backend}' if version == 'before'
                     else f'/tmp/coin-render-first-frame-{backend}')
            env['LD_LIBRARY_PATH'] = build + '/lib'
            stem = f'{variant}-{version}-{pair}'
            cmd = [build + '/bin/coin_render_gl_benchmark', '--scene',
                   '/tmp/coin-render-city-mixed-depth-10000.iv', '--backend', backend,
                   '--size', '1024', '--warmup', '1', '--frames', '2']
            if pair == 1:
                cmd += ['--image-output', str(out / (stem + '.ppm'))]
            result = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
            (out / (stem + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'{stem}: code {result.returncode}: {result.stderr[-2000:]}')
            first = re.search(r'^\S+_first_frame_ms=([\d.]+)', result.stdout, re.M)
            steady = re.search(r'^\S+ frames=2 median_ms=([\d.]+) p95_ms=([\d.]+)', result.stdout, re.M)
            checksum = re.search(r'rgba_fnv64=(\S+)', result.stdout)
            row = dict(variant=variant, gpu=gpu, version=version, pair=pair,
                       first_ms=float(first[1]), median_ms=float(steady[1]),
                       p95_ms=float(steady[2]), checksum=checksum[1])
            if pair == 1:
                row['image_sha256'] = hashlib.sha256((out/(stem+'.ppm')).read_bytes()).hexdigest()
            rows.append(row)
            (out/'results.json').write_text(json.dumps(rows, indent=2)+'\n')
            print(f'{stem}: first={row["first_ms"]:.2f} ms steady={row["median_ms"]:.2f} checksum={row["checksum"]}', flush=True)

for variant, _, _, _ in variants[2:3]:
    records = [r for r in rows if r['variant'] == variant]
    assert len({r['checksum'] for r in records}) == 1, variant
    images = [r['image_sha256'] for r in records if r['pair'] == 1]
    assert len(set(images)) == 1, variant
print('All six fallback checksums and the before/after image match',flush=True)
