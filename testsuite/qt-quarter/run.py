#!/usr/bin/env python3
"""Run each native GPU regression in a fresh process; never turn skips green."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

CASES = ('first-expose', 'frame-coalescing', 'idle', 'resize', 'maximize',
         'minimize', 'panel', 'dpr', 'recreate', 'wheel-rotation',
         'two-viewports', 'navicube', 'depth', 'polygon-offset', 'annotation',
         'foregroundroot', 'decorationroot', 'axis-cross', 'rubber-band')
EXIT = {'PASS': 0, 'SKIP': 77, 'UNSUPPORTED': 78, 'FAIL': 1}

def execute(command, env, timeout):
    # FreeCAD may launch helper processes which otherwise retain stdout and
    # survive a parent-only timeout. Own and terminate the complete test group.
    with subprocess.Popen(command, env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, start_new_session=True) as proc:
        try:
            output, _ = proc.communicate(timeout=timeout)
            return proc.returncode, output
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            output, _ = proc.communicate()
            return -signal.SIGKILL, output + '\nHARNESS_TIMEOUT\n'

def classify(code, output):
    """Exit and structured result must agree; initialization is not submission."""
    matches = re.findall(r'^RESULT (.+)$', output, re.MULTILINE)
    try:
        result = json.loads(matches[-1]) if matches else {}
    except (ValueError, IndexError):
        result = {}
    if result.get('status') not in EXIT or EXIT.get(result.get('status')) != code:
        return dict(status='FAIL', reason=f'missing/inconsistent RESULT; process exit {code}')
    if result['status'] == 'PASS' and 'COIN_WGPU_PHASE bgfx lower_ms=' not in output:
        return dict(status='FAIL', reason='no BGFX submission evidence (GL fallback is not a pass)')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', required=True, type=Path)
    parser.add_argument('--artifacts', required=True, type=Path)
    parser.add_argument('--renderer', action='append', choices=['vulkan', 'opengl'])
    parser.add_argument('--case', action='append', choices=CASES + ('hover',))
    parser.add_argument('--freecad', type=Path, help='FreeCAD executable for real Face1 -> Face2 preselection')
    parser.add_argument('--timeout', type=int, default=45)
    parser.add_argument('--require-hardware', action='store_true')
    args = parser.parse_args()
    args.artifacts = args.artifacts.resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    inventory = {}
    for name, command in [('gl', ['glxinfo', '-B']), ('vulkan', ['vulkaninfo', '--summary'])]:
        try:
            proc = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=15)
            inventory[name] = proc.stdout
        except (OSError, subprocess.TimeoutExpired) as error:
            inventory[name] = str(error)
    (args.artifacts / 'inventory.json').write_text(json.dumps(inventory, indent=2))
    provenance = {'harness': str(args.harness.resolve()),
                  'harness_sha256': hashlib.sha256(args.harness.read_bytes()).hexdigest() if args.harness.is_file() else None,
                  'environment': {key: os.environ.get(key) for key in
                    ('DISPLAY', 'LD_LIBRARY_PATH', 'VK_ICD_FILENAMES', 'VK_DRIVER_FILES')}}
    (args.artifacts / 'provenance.json').write_text(json.dumps(provenance, indent=2))
    results = []
    cases = args.case or list(CASES) + ['hover']
    for renderer in args.renderer or ['vulkan', 'opengl']:
        description = inventory['gl' if renderer == 'opengl' else 'vulkan'].lower()
        software = any(word in description for word in ('llvmpipe', 'lavapipe', 'softpipe', 'cpu'))
        # A mixed Vulkan inventory is NOT evidence that the selected adapter is hardware.
        for case in cases:
            variants = [(mode, alpha, scale) for mode in ('object', 'weighted_oit')
                        for alpha in ('opaque', 'translucent') for scale in (1,)] if case == 'navicube' else [('object', 'opaque', s) for s in ((1, 2) if case == 'dpr' else (1,))]
            for mode, alpha, scale in variants:
                directory = args.artifacts / f'{renderer}-{mode}-{alpha}-{scale}x-{case}'
                directory.mkdir(exist_ok=True)
                env = dict(os.environ, FREECAD_COIN_WGPU='1', QT_QPA_PLATFORM='xcb',
                           COIN_WGPU_TRACE_PHASES='1', COIN_BGFX_RENDERER=renderer,
                           COIN_BGFX_TRANSPARENCY=mode, COIN_TEST_ALPHA=alpha,
                           QT_SCALE_FACTOR=str(scale), COIN_TEST_ARTIFACTS=str(directory))
                if not env.get('DISPLAY'):
                    result = dict(status='SKIP', reason='no DISPLAY; native X11 surface cannot execute')
                    output = ''
                elif case == 'hover' and not args.freecad:
                    result = dict(status='UNSUPPORTED', reason='--freecad required for real FreeCAD preselection')
                    output = ''
                else:
                    command = [str(args.harness.resolve()), case]
                    if case == 'hover':
                        command = [str(args.freecad.resolve()), str(Path(__file__).with_name('freecad_hover.FCMacro'))]
                    try:
                        code, output = execute(command, env, args.timeout)
                        result = classify(code, output)
                        if 'HARNESS_TIMEOUT' in output:
                            result = dict(status='FAIL', reason=f'timeout {args.timeout}s; possible infinite frame loop')
                    except OSError as error:
                        output = str(error)
                        result = dict(status='FAIL', reason='test executable could not start')
                (directory / 'process.log').write_text(output)
                devices = re.findall(r'COIN_WGPU_PHASE bgfx_device renderer=(\w+) vendor_id=(0x[0-9a-f]+) device_id=(0x[0-9a-f]+)', output)
                if result['status'] == 'PASS' and (not devices or any(d[0] != renderer for d in devices)):
                    result.update(status='FAIL', reason='requested renderer was not proven; fallback is forbidden')
                if devices and not result.get('adapter'):
                    actual_renderer, vendor, device = devices[-1]
                    result['adapter'] = f'BGFX {actual_renderer} vendor {vendor} device {device}'
                adapter = str(result.get('adapter', '')).lower()
                actual_software = any(word in adapter for word in ('llvmpipe', 'lavapipe', 'softpipe', 'cpu')) or (renderer == 'opengl' and software)
                known_vendor = bool(devices) and all(d[1] in ('0x1002', '0x10de', '0x8086', '0x13b5', '0x5143') for d in devices)
                hardware = not actual_software and bool(devices) and (known_vendor or
                            (renderer == 'opengl' and 'accelerated: yes' in description))
                submitted = 'COIN_WGPU_PHASE bgfx lower_ms=' in output
                result.update(test=case, renderer=renderer, mode=mode, alpha=alpha,
                              scale=scale, hardware_gpu=hardware and submitted,
                              gpu_submitted=submitted,
                              gpu_execution=('hardware' if hardware else 'software-or-unverified') if submitted else 'not-submitted', artifacts=str(directory))
                if args.require_hardware and result['status'] == 'PASS' and not hardware:
                    result.update(status='FAIL', reason='physical GPU execution was not proven')
                (directory / 'result.json').write_text(json.dumps(result, indent=2))
                results.append(result)
                print(f"{result['status']:11} {directory.name}: {result.get('reason', '')}", flush=True)
    (args.artifacts / 'results.json').write_text(json.dumps(results, indent=2))
    counts = {status: sum(r['status'] == status for r in results) for status in EXIT}
    print(json.dumps(counts))
    return next((EXIT[status] for status in ('FAIL', 'SKIP', 'UNSUPPORTED') if counts[status]), 0)

if __name__ == '__main__':
    sys.exit(main())
