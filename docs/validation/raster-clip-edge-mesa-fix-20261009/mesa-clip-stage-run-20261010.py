#!/usr/bin/env python3
"""Run the installed Mesa raster clip stage controls."""
import os
import pathlib
import re
import subprocess

log_path = pathlib.Path('/tmp/mesa-clip-stage-20261010.log')
base = os.environ.copy()
base.update({
    'EGL_PLATFORM': 'surfaceless',
    '__EGL_VENDOR_LIBRARY_FILENAMES': '/usr/share/glvnd/egl_vendor.d/50_mesa.json',
})
cases = []
for renderer, extra, expected in (
    ('amd', {}, 0),
    ('llvmpipe', {'LIBGL_ALWAYS_SOFTWARE': '1'}, 33),
):
    for stage in ('vs', 'gs'):
        cases.append((f'{renderer}-{stage}', ['/tmp/mesa-clip-stage-probe', stage],
                      extra, f'x=31 lit={expected}', 'retained-edge left=0 right=22'))
    cases.append((f'{renderer}-one-pass', ['/tmp/mesa-one-pass-clip-probe'],
                  extra, 'vertical=17 horizontal=17', 'error=0'))
    cases.append((f'{renderer}-one-pass-points', ['/tmp/mesa-one-pass-point-probe'],
                  extra, 'point 32,32 lit=16', 'point 57,57 lit=16'))

all_pass = True
with log_path.open('w') as log:
    for name, command, extra, expected1, expected2 in cases:
        run = subprocess.run(command, env=base | extra, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, timeout=30)
        ok = run.returncode == 0 and expected1 in run.stdout and expected2 in run.stdout
        all_pass &= ok
        result = f'CASE {name} exit={run.returncode} result={"PASS" if ok else "FAIL"}\n'
        log.write(result)
        log.write(run.stdout)
        log.write('\n')
        print(result.strip(), flush=True)
print(log_path)
raise SystemExit(0 if all_pass else 1)
