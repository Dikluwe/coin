#!/usr/bin/env python3
"""Run one process with the privately installed Renoir sampling prototype."""
from pathlib import Path
import argparse, os, subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix', required=True, type=Path)
p.add_argument('--disabled', action='store_true')
p.add_argument('--gl-compiler', choices=['aco', 'llvm-vs-aco-ps'], default='aco')
p.add_argument('command', nargs=argparse.REMAINDER)
a = p.parse_args()
command = a.command[1:] if a.command[:1] == ['--'] else a.command
if not command: p.error('provide a program after --')
prefix = a.prefix.resolve()
lib = prefix / 'lib/x86_64-linux-gnu'
egl = prefix / 'share/glvnd/egl_vendor.d/50_mesa.json'
vk = prefix / 'share/vulkan/icd.d/radeon_icd.x86_64.json'
for path in [lib / 'dri/radeonsi_dri.so', egl, vk]:
    if not path.is_file(): p.error('private Mesa file missing: ' + str(path))
env = dict(os.environ)
for key in ['__NV_PRIME_RENDER_OFFLOAD', 'AMD_DEBUG', 'AMD_FORCE_SHADER_USE_ACO', 'COIN_MESA_MIXED_FILTER_STUDY']:
    env.pop(key, None)
env.update(LD_LIBRARY_PATH=str(lib) + ':' + env.get('LD_LIBRARY_PATH', ''),
           LIBGL_DRIVERS_PATH=str(lib / 'dri'), __EGL_VENDOR_LIBRARY_FILENAMES=str(egl),
           __GLX_VENDOR_LIBRARY_NAME='mesa', VK_DRIVER_FILES=str(vk), VK_ICD_FILENAMES=str(vk),
           MESA_SHADER_CACHE_DISABLE='true', NIR_DEBUG='validate')
if not a.disabled: env['COIN_MESA_MIXED_FILTER_STUDY'] = '1'
if a.gl_compiler == 'llvm-vs-aco-ps': env.update(AMD_DEBUG='llvm', AMD_FORCE_SHADER_USE_ACO='ps')
raise SystemExit(subprocess.run(command, env=env).returncode)
