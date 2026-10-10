#!/usr/bin/env python3
import os
import pathlib
import subprocess

out = pathlib.Path('/tmp/coin-mesa-validation-20261010.log')
mesa_lib = '/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-failure-fixes/mesa-study/install/lib/x86_64-linux-gnu'
candidate = os.environ.copy()
candidate.update({
    'LD_LIBRARY_PATH': '/tmp/coin-mesa-finish-lib:' + mesa_lib,
    'LIBGL_DRIVERS_PATH': '/tmp/coin-mesa-private-dri-20261009',
    'MESA_EXPERIMENTAL_CLIP_BOUNDARY': '1',
    'MESA_SHADER_CACHE_DISABLE': 'true',
    'AMD_DEBUG': 'nongg',
    '__EGL_VENDOR_LIBRARY_FILENAMES': '/usr/share/glvnd/egl_vendor.d/50_mesa.json',
    'EGL_PLATFORM': 'surfaceless',
})
cases = []
for planes in (1, 2, 5, 8):
    cases.append((f'texture-{planes}-candidate', ['/tmp/coin-mesa-multiplane-texture', str(planes)], candidate, 0))
cases.append(('fog-2-candidate', ['/tmp/coin-mesa-multiplane-fog', '2'], candidate, 0))
cases.append(('strict-oracle-candidate', ['/tmp/coin-raster-clip-edge-gl-strict', '--strict-boundary'], candidate, 0))
coingl = candidate.copy()
coingl['LD_LIBRARY_PATH'] = '/tmp/coin-mesa-finish-lib:/tmp/coin-original-coingl-20261009/build/lib:' + mesa_lib
coingl.update({'COIN_RENDER_REQUIRE_GL_REFERENCE': '1', 'COIN_GLXGLUE_NO_PBUFFERS': '1', 'COIN_GLX_PIXMAP_DIRECT_RENDERING': '1'})
cases.append(('coingl-original-clip-probe', ['/tmp/coin-original-coingl-20261009/build/bin/CoinRenderDrawStyleTest', '--probe-clipped-boundary'], coingl, 0))
without = candidate.copy()
without.pop('MESA_EXPERIMENTAL_CLIP_BOUNDARY')
cases.append(('texture-2-without-flag', ['/tmp/coin-mesa-multiplane-texture', '2'], without, 1))
software = os.environ.copy()
software.update({'LIBGL_ALWAYS_SOFTWARE':'1', 'EGL_PLATFORM':'surfaceless',
                 '__EGL_VENDOR_LIBRARY_FILENAMES':'/usr/share/glvnd/egl_vendor.d/50_mesa.json'})
cases.append(('texture-2-llvmpipe-control', ['/tmp/coin-mesa-multiplane-texture', '2'], software, 0))
cases.append(('fog-2-llvmpipe-control', ['/tmp/coin-mesa-multiplane-fog', '2'], software, 0))
passed = True
with out.open('w') as log:
    for name, cmd, env, expected in cases:
        result = subprocess.run(cmd, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=60)
        ok = result.returncode == expected
        print(f'CASE {name} exit={result.returncode} expected={expected} result={"PASS" if ok else "FAIL"}', flush=True)
        log.write(f'CASE {name} exit={result.returncode} expected={expected} result={"PASS" if ok else "FAIL"}\n')
        log.write(result.stdout)
        log.write('\n')
        passed &= ok
print(out)
raise SystemExit(0 if passed else 1)
