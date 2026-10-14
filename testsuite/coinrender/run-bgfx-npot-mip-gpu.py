#!/usr/bin/env python3
"""Measure BGFX direct NPOT mip GPU frames in the eight-shadow fixture."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess


STAGE = re.compile(
    r'^COIN_RENDER_PHASE rtt_npot_mips_gpu levels=(\d+) '
    r'area_resolved=(\d+) area_ms=([^ ]+) '
    r'frame_resolved=(\d+) frame_ms=([^ ]+)(.*)$')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def percentile95(values):
    return sorted(values)[math.ceil(len(values) * 0.95) - 1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--full-logs', required=True, type=Path)
    parser.add_argument('--display', default=os.environ.get('DISPLAY', ':0'))
    parser.add_argument('--xauthority', default=os.environ.get('XAUTHORITY', ''))
    args = parser.parse_args()
    build = args.build.resolve()
    output = args.output.resolve()
    full_logs = args.full_logs.resolve()
    binary = build / 'bin/CoinRenderShadowReferenceTest'
    if not binary.is_file():
        parser.error('missing test binary: ' + str(binary))
    output.mkdir(parents=True, exist_ok=True)
    full_logs.mkdir(parents=True, exist_ok=True)
    rows = []
    for gpu in ('amd', 'nvidia'):
        for api in ('vulkan', 'opengl'):
            for mechanism, name in ((1, 'peeling'), (2, 'weighted')):
                env = dict(os.environ)
                for key in ('__NV_PRIME_RENDER_OFFLOAD', 'EGL_PLATFORM',
                            'COIN_SAMPLING_STUDY', 'COIN_BGFX_TRACE_GL_ADAPTER'):
                    env.pop(key, None)
                icd = '/usr/share/vulkan/icd.d/' + (
                    'radeon_icd.json' if gpu == 'amd' else 'nvidia_icd.json')
                env.update(DISPLAY=args.display, XAUTHORITY=args.xauthority,
                           COIN_BGFX_RENDERER=api,
                           COIN_GLX_PIXMAP_DIRECT_RENDERING='1',
                           COIN_GLXGLUE_NO_PBUFFERS='1',
                           COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU='1',
                           COIN_RENDER_TRACE_PHASES='1',
                           COIN_WGPU_GPU_TIMESTAMPS='1',
                           LD_LIBRARY_PATH=str(build / 'lib'),
                           VK_DRIVER_FILES=icd, VK_ICD_FILENAMES=icd,
                           __GLX_VENDOR_LIBRARY_NAME=('mesa' if gpu == 'amd' else 'nvidia'),
                           __EGL_VENDOR_LIBRARY_FILENAMES=
                           '/usr/share/glvnd/egl_vendor.d/' + (
                               '50_mesa.json' if gpu == 'amd' else '10_nvidia.json'))
                if gpu == 'nvidia':
                    env['__NV_PRIME_RENDER_OFFLOAD'] = '1'
                if api == 'opengl':
                    env['COIN_BGFX_TRACE_GL_ADAPTER'] = '1'
                if gpu == 'nvidia' and api == 'opengl':
                    env['EGL_PLATFORM'] = 'surfaceless'
                command = [str(binary), '--npot-shadow-oit-bench', str(mechanism)]
                stem = f'{gpu}-{api}-{name}'
                full_log = full_logs / f'{stem}.log'
                try:
                    with full_log.open('w') as stream:
                        code = subprocess.run(command, env=env, cwd=full_logs,
                                              stdout=stream,
                                              stderr=subprocess.STDOUT,
                                              timeout=240).returncode
                except subprocess.TimeoutExpired:
                    code = 124
                lines = full_log.read_text(errors='replace').splitlines()
                adapter = next((line for line in lines if line.startswith(
                    'NPOT combined adapter=')), '')
                gl_adapter = next((line for line in lines if
                    'bgfx_gl_adapter ' in line), '')
                benchmark = next((line for line in lines if line.startswith(
                    'NPOT shadow mip benchmark')), '')
                finish = next((line for line in lines if line.startswith(
                    'NPOT shadow transparency')), '')
                stage_lines = [line for line in lines if STAGE.match(line)]
                samples = []
                for line in stage_lines:
                    match = STAGE.match(line)
                    levels, area_resolved, area, frame_resolved, frame, tail = match.groups()
                    per_level = {int(level): float(value) for level, value in
                                 re.findall(r'level(\d+)_ms=([0-9.]+)', tail)}
                    samples.append(dict(levels=int(levels),
                                        area_resolved=int(area_resolved),
                                        area_ms=float(area) if area != 'unavailable' else None,
                                        frame_resolved=int(frame_resolved),
                                        frame_ms=float(frame) if frame != 'unavailable' else None,
                                        per_level_ms=per_level))
                selected = samples[-30:]
                valid = (code == 0 and len(samples) == 44 and
                         ('AMD' if gpu == 'amd' else 'NVIDIA') in
                         (gl_adapter if api == 'opengl' else adapter) and
                         f'mechanism={mechanism}' in benchmark and
                         'qualified=1' in finish and len(selected) == 30 and
                         all(s['levels'] == 5 and s['area_resolved'] == 5 and
                             s['frame_resolved'] == 5 and
                             len(s['per_level_ms']) == 5 for s in selected))
                reduced = output / f'{stem}.log'
                reduced.write_text('\n'.join(
                    [adapter, gl_adapter, *stage_lines, benchmark, finish,
                     f'exit={code} full_log_sha256={sha256(full_log)}']) + '\n')
                row = dict(gpu=gpu, api=api, mechanism=name, exit=code,
                           pass_gate=valid, adapter=adapter, gl_adapter=gl_adapter,
                           benchmark=benchmark, finish=finish,
                           samples=len(samples), measured_samples=len(selected),
                           log=reduced.name, log_sha256=sha256(reduced),
                           full_log=str(full_log), full_log_sha256=sha256(full_log))
                if valid:
                    for key in ('area_ms', 'frame_ms'):
                        values = [sample[key] for sample in selected]
                        row[key + '_median'] = statistics.median(values)
                        row[key + '_p95'] = percentile95(values)
                    row['area_level_medians_ms'] = {
                        str(level): statistics.median(
                            sample['per_level_ms'][level] for sample in selected)
                        for level in range(1, 6)}
                rows.append(row)
                (output / 'summary.json').write_text(json.dumps(dict(
                    binary=str(binary), binary_sha256=sha256(binary),
                    full_logs=str(full_logs), results=rows), indent=2) + '\n')
                print(stem, 'PASS' if valid else 'FAIL',
                      row.get('frame_ms_median'), flush=True)
                if not valid:
                    return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
