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

CASES = ('rtt-window', 'first-expose', 'frame-coalescing', 'idle', 'resize', 'maximize',
         'minimize', 'panel', 'dpr', 'recreate', 'wheel-rotation',
         'two-viewports', 'freecad-multi', 'freecad-viewport', 'freecad-screen-content', 'freecad-sampling-policy', 'freecad-overlays', 'freecad-legacy-polyline', 'freecad-flags', 'freecad-delayed-overlays', 'freecad-grid', 'freecad-path-selection', 'freecad-mouse-picking', 'freecad-mouse-elements', 'freecad-mouse-links', 'freecad-mouse-link-topology', 'freecad-selection-menu', 'retained-rejection', 'colorbar', 'navicube', 'depth', 'polygon-offset', 'annotation',
         'foregroundroot', 'decorationroot', 'axis-cross', 'rubber-band')
EXIT = {'PASS': 0, 'REFERENCE_PASS': 0, 'SKIP': 77, 'UNSUPPORTED': 78, 'FAIL': 1}
OFFSCREEN_CASES = ('retained-rejection', 'colorbar', 'navicube', 'depth', 'polygon-offset', 'annotation',
                   'foregroundroot', 'decorationroot', 'axis-cross', 'rubber-band')

def bgfx_gl_adapter_evidence(output):
    adapters = re.findall(r'^COIN_RENDER_PHASE bgfx_gl_adapter vendor=(.*?) renderer=(.*?) version=(.*)$',
                          output, re.MULTILINE)
    hardware = bool(adapters)
    for vendor, renderer, version in adapters:
        text = (vendor + ' ' + renderer).lower()
        hardware = hardware and bool(version) and not any(
            word in text for word in ('llvmpipe','lavapipe','softpipe','software','virgl','cpu')) and any(
            word in text for word in ('nvidia geforce','nvidia quadro','nvidia rtx','amd radeon','mesa intel','intel(r)'))
    return adapters, hardware

def wgpu_gl_adapter_evidence(output):
    # wgpu's GLES adapter can report Other/device 0 for physical GPUs. Require
    # the submitted context's renderer name and known vendor, never Qt inventory.
    records = re.findall(r'COIN_RENDER_PHASE wgpu_surface renderer=opengl '
                         r'vendor_id=(0x[0-9a-f]+) device_id=0x[0-9a-f]+ '
                         r'device_type=(\w+) .*?adapter_name="([^"\n]+)"', output)
    physical = bool(records)
    for vendor, kind, name in records:
        lower = name.lower()
        physical = physical and vendor in ('0x1002', '0x10de', '0x8086') and kind in ('Other', 'IntegratedGpu', 'DiscreteGpu') and not any(
            word in lower for word in ('llvmpipe', 'lavapipe', 'softpipe', 'software', 'virgl', 'cpu')) and any(
            word in lower for word in ('amd radeon', 'nvidia geforce', 'nvidia quadro', 'nvidia rtx', 'intel'))
    return records, physical

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

def classify(code, output, require_bgfx=True, backend="bgfx", offscreen=False):
    """Exit and structured result must agree; initialization is not submission."""
    matches = re.findall(r'^RESULT (.+)$', output, re.MULTILINE)
    try:
        result = json.loads(matches[-1]) if matches else {}
    except (ValueError, IndexError):
        result = {}
    if result.get('status') not in EXIT or EXIT.get(result.get('status')) != code:
        return dict(status='FAIL', reason=f'missing/inconsistent RESULT; process exit {code}')
    if require_bgfx and (result.get('reference_gl') is True or result['status'] == 'REFERENCE_PASS'):
        return dict(status='FAIL', reason='Coin/GL reference cannot count as GPU backend validation')
    if require_bgfx and result['status'] == 'PASS':
        if 'switching viewport to Coin/GL' in output:
            return dict(status='FAIL', reason='native viewport fell back to Coin/GL')
        if not submission_evidence(output, backend, offscreen):
            return dict(status='FAIL', reason=f'no {backend} submission evidence (GL fallback is not a pass)')
    return result

def submission_evidence(output, backend, offscreen=False):
    if backend == 'wgpu':
        if offscreen:
            return bool(re.search(r'COIN_RENDER_PHASE wgpu_offscreen renderer=\w+ '
                                  r'vendor_id=0x[0-9a-f]+ device_id=0x[0-9a-f]+ '
                                  r'device_type=\w+ serial=[1-9][0-9]* size=[1-9][0-9]*x[1-9][0-9]*', output))
        return bool(re.search(r'COIN_RENDER_PHASE wgpu_surface renderer=\w+ '
                              r'vendor_id=0x[0-9a-f]+ device_id=0x[0-9a-f]+ '
                              r'device_type=\w+ surface=[1-9][0-9]* serial=[1-9][0-9]* '
                              r'size=[1-9][0-9]*x[1-9][0-9]*', output))
    return 'COIN_RENDER_PHASE bgfx lower_ms=' in output


def device_evidence(output, backend, offscreen=False):
    phase = ('wgpu_offscreen' if offscreen else 'wgpu_surface') if backend == 'wgpu' else 'bgfx_device'
    devices = re.findall(r'COIN_RENDER_PHASE ' + phase +
                         r' renderer=([^\n]+?) vendor_id=(0x[0-9a-f]+) device_id=(0x[0-9a-f]+)', output)
    names = {'OpenGL 4.3': 'opengl', 'OpenGL 2.1': 'opengl',
             'OpenGL': 'opengl', 'Vulkan': 'vulkan'}
    return [(names.get(renderer, renderer), vendor, device)
            for renderer, vendor, device in devices]


def window_manager_available(output):
    """EWMH proof that maximize/restore semantics can be tested."""
    return bool(re.search(
        r'_NET_SUPPORTING_WM_CHECK.*window id # 0x(?!0(?:\\s|$))[0-9a-f]+',
        output, re.IGNORECASE))

def parse_lock_state(output):
    match = re.search(r'^\s*boolean (true|false)\s*$', output, re.MULTILINE)
    return match.group(1) == 'true' if match else None

def session_locked():
    # Read-only probe. Never unlock the session or change screensaver settings.
    for service, path in (
            ('org.cinnamon.ScreenSaver', '/org/cinnamon/ScreenSaver'),
            ('org.freedesktop.ScreenSaver', '/org/freedesktop/ScreenSaver'),
            ('org.gnome.ScreenSaver', '/org/gnome/ScreenSaver')):
        try:
            owner = subprocess.run(
                ['dbus-send', '--session', '--dest=org.freedesktop.DBus',
                 '--type=method_call', '--print-reply', '/org/freedesktop/DBus',
                 'org.freedesktop.DBus.NameHasOwner', 'string:' + service],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1)
            if owner.returncode != 0 or parse_lock_state(owner.stdout) is not True:
                continue  # Never auto-start a screensaver just to read its state.
            probe = subprocess.run(
                ['dbus-send', '--session', '--dest=' + service, '--type=method_call',
                 '--print-reply', path, service + '.GetActive'],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1)
            state = parse_lock_state(probe.stdout) if probe.returncode == 0 else None
            if state is not None:
                return state
        except (OSError, subprocess.TimeoutExpired):
            pass
    return None

def select_variants(variants, modes=None, scales=None):
    selected = [v for v in variants
                if (not modes or v[0] in modes)
                and (not scales or v[2] in scales)]
    if not selected:
        raise ValueError('requested filters select no test variants')
    return selected


def checkpoint_results(directory, results):
    checkpoint = directory / 'results.json.tmp'
    checkpoint.write_text(json.dumps(results, indent=2))
    checkpoint.replace(directory / 'results.json')

def prepare_profile(profile, case):
    profile.mkdir(exist_ok=True)
    user = profile / 'user.cfg'
    if user.exists():
        return
    # Profiling writes diagnostics which FreeCAD forwards to notifications.
    # Disable that UI in our owned test profile before it can cover captures.
    dock = ('<FCParamGroup Name="DockWindows"><FCBool Name="ActivateOverlay" Value="0"/>'
            '</FCParamGroup>') if case in ('freecad-mouse-links', 'freecad-mouse-link-topology',
                                         'freecad-viewport') else ''
    user.write_text('<?xml version="1.0" encoding="UTF-8"?>\n'
                    '<FCParameters><FCParamGroup Name="Root"><FCParamGroup Name="BaseApp">'
                    '<FCParamGroup Name="Preferences"><FCParamGroup Name="NotificationArea">'
                    '<FCBool Name="NotificationAreaEnabled" Value="0"/>'
                    '<FCBool Name="NonIntrusiveNotificationsEnabled" Value="0"/>'
                    '</FCParamGroup>' + dock +
                    '</FCParamGroup></FCParamGroup></FCParamGroup></FCParameters>\n')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', required=True, type=Path)
    parser.add_argument('--artifacts', required=True, type=Path)
    parser.add_argument('--backend', choices=['bgfx', 'wgpu'], default='bgfx',
                        help='backend compiled into the tested CoinRender library')
    parser.add_argument('--renderer', action='append', choices=['vulkan', 'opengl'])
    parser.add_argument('--mode', action='append', choices=['object', 'weighted_oit'],
                        help='restrict variants for a targeted retry')
    parser.add_argument('--scale', action='append', type=int, choices=[1, 2],
                        help='restrict DPR variants for a targeted retry')
    parser.add_argument('--case', action='append', choices=CASES + ('hover',))
    parser.add_argument('--freecad', type=Path, help='FreeCAD executable for real Face1 -> Face2 preselection')
    parser.add_argument('--timeout', type=int, default=45)
    parser.add_argument('--require-hardware', action='store_true')
    parser.add_argument('--reference-gl', action='store_true',
                        help='Coin/GL-only document baseline; never reports native backend PASS')
    args = parser.parse_args()
    if args.backend == 'wgpu' and (args.reference_gl or (args.renderer not in (None, ['vulkan']) and
            not (args.case == ['freecad-sampling-policy'] and args.renderer == ['opengl']))):
        parser.error('the current wgpu native profile requires --renderer vulkan and no --reference-gl')
    if args.reference_gl and (args.case not in (['freecad-mouse-links'], ['freecad-mouse-link-topology'], ['freecad-viewport'], ['freecad-multi'], ['freecad-grid'], ['freecad-path-selection'], ['freecad-mouse-elements'], ['freecad-screen-content'], ['freecad-sampling-policy']) or
                              args.renderer != ['opengl'] or args.require_hardware or
                              args.mode not in (None, ['object'])):
        parser.error('--reference-gl requires one reference-capable FreeCAD case, --renderer opengl, and no --require-hardware')
    args.artifacts = args.artifacts.resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    inventory = {}
    for name, command in [('gl', ['glxinfo', '-B']), ('vulkan', ['vulkaninfo', '--summary']),
                          ('window_manager', ['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'])]:
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
                    ('DISPLAY', 'LD_LIBRARY_PATH', 'VK_ICD_FILENAMES', 'VK_DRIVER_FILES',
                     'EGL_PLATFORM', '__EGL_VENDOR_LIBRARY_FILENAMES', '__GLX_VENDOR_LIBRARY_NAME',
                     'DRI_PRIME', 'LIBGL_ALWAYS_SOFTWARE',
                     'MESA_LOADER_DRIVER_OVERRIDE', 'QT_QPA_PLATFORM',
                     'QT_OPENGL', 'QT_SCALE_FACTOR', '__EGL_VENDOR_LIBRARY_FILENAMES',
                     'COIN_TEST_RETAINED_NODES', 'COIN_TEST_RETAINED_FILTER',
                     'COIN_TEST_SPLINE_CONSUMER', 'COIN_TEST_SPLINE_KIND',
                     'COIN_BGFX_DISABLE_PROGRAM_CACHE', 'COIN_RENDER_RTT_GPU_DIRECT')}}
    (args.artifacts / 'provenance.json').write_text(json.dumps(provenance, indent=2))
    window_manager = window_manager_available(inventory['window_manager'])
    results = []
    cases = args.case or list(CASES) + ['hover']
    for renderer in args.renderer or (['vulkan'] if args.backend == 'wgpu' else ['vulkan', 'opengl']):
        description = inventory['gl' if renderer == 'opengl' else 'vulkan'].lower()
        software = any(word in description for word in ('llvmpipe', 'lavapipe', 'softpipe', 'cpu'))
        # A mixed Vulkan inventory is NOT evidence that the selected adapter is hardware.
        for case in cases:
            variants = [(mode, alpha, scale) for mode in ('object', 'weighted_oit')
                        for alpha in ('opaque', 'translucent') for scale in (1,)] if case == 'navicube' else [('object', 'opaque', s) for s in ((1, 2) if case == 'dpr' else (1,))]
            if case in ('two-viewports', 'freecad-multi', 'freecad-viewport', 'freecad-legacy-polyline', 'freecad-delayed-overlays'):
                variants = [(mode, 'opaque', 1) for mode in ('object', 'weighted_oit')]
            if case in ('freecad-screen-content', 'freecad-sampling-policy', 'freecad-overlays', 'freecad-flags', 'freecad-grid', 'freecad-path-selection', 'freecad-mouse-picking', 'freecad-mouse-elements', 'freecad-mouse-links', 'freecad-mouse-link-topology', 'freecad-selection-menu'):
                variants = [(mode, 'opaque', scale) for mode in ('object', 'weighted_oit')
                            for scale in (1, 2)]
            if case == 'rtt-window':
                variants=[(mode,'opaque',scale) for mode in ('object','weighted_oit') for scale in (1,2)]
            if case == 'freecad-viewport':
                variants = [(mode, 'opaque', scale) for mode in ('object', 'weighted_oit')
                            for scale in (1, 2)]
            if args.reference_gl:
                variants = [('object', 'opaque', scale) for scale in (1, 2)]
            try:
                variants = select_variants(variants, args.mode, args.scale)
            except ValueError as error:
                parser.error(f'{case}: {error}')
            for mode, alpha, scale in variants:
                directory = args.artifacts / f'{renderer}-{mode}-{alpha}-{scale}x-{case}'
                directory.mkdir(exist_ok=True)
                env = dict(os.environ, FREECAD_COIN_WGPU='0' if args.reference_gl else '1',
                           COIN_TEST_GL_REFERENCE='1' if args.reference_gl else '0',
                           QT_QPA_PLATFORM='xcb',
                           COIN_RENDER_TRACE_PHASES='1', COIN_BGFX_RENDERER=renderer,
                           COIN_BGFX_TRANSPARENCY=mode, COIN_RENDER_TRANSPARENCY=mode, COIN_TEST_ALPHA=alpha,
                           QT_SCALE_FACTOR=str(scale), COIN_TEST_ARTIFACTS=str(directory),
                           COIN_TEST_MACRO_DIR=str(Path(__file__).resolve().parent))
                if case=='rtt-window':env['COIN_RENDER_RTT_GPU_DIRECT']='1'
                if args.backend == 'bgfx' and renderer == 'opengl' and sys.platform.startswith('linux'):
                    env['COIN_BGFX_TRACE_GL_ADAPTER'] = '1'
                if not env.get('DISPLAY'):
                    result = dict(status='SKIP', reason='no DISPLAY; native X11 surface cannot execute')
                    output = ''
                elif case in ('hover', 'freecad-multi', 'freecad-viewport', 'freecad-screen-content', 'freecad-sampling-policy', 'freecad-overlays', 'freecad-legacy-polyline', 'freecad-flags', 'freecad-grid', 'freecad-path-selection', 'freecad-mouse-picking', 'freecad-mouse-elements', 'freecad-mouse-links', 'freecad-mouse-link-topology', 'freecad-selection-menu') and session_locked() is True:
                    result = dict(status='SKIP', reason='desktop session locked; screen capture unavailable')
                    output = ''
                elif case == 'maximize' and not window_manager:
                    result = dict(status='SKIP', reason='no EWMH window manager; maximize/restore cannot be verified')
                    output = ''
                elif case in ('hover', 'freecad-multi', 'freecad-viewport', 'freecad-screen-content', 'freecad-sampling-policy', 'freecad-overlays', 'freecad-legacy-polyline', 'freecad-flags', 'freecad-grid', 'freecad-path-selection', 'freecad-mouse-picking', 'freecad-mouse-elements', 'freecad-mouse-links', 'freecad-mouse-link-topology', 'freecad-selection-menu', 'freecad-delayed-overlays') and not args.freecad:
                    result = dict(status='UNSUPPORTED', reason='--freecad required for real FreeCAD viewport tests')
                    output = ''
                elif case in ('freecad-delayed-overlays', 'freecad-selection-menu') and not Path(env.get('COIN_TEST_DELAYED_HELPER', '')).is_file():
                    result = dict(status='UNSUPPORTED', reason='COIN_TEST_DELAYED_HELPER is required')
                    output = ''
                elif case == 'freecad-flags' and not Path(env.get('COIN_TEST_FLAG_HELPER', '')).is_file():
                    result = dict(status='UNSUPPORTED', reason='COIN_TEST_FLAG_HELPER shared library required')
                    output = ''
                else:
                    command = [str(args.harness.resolve()), case]
                    if case in ('hover', 'freecad-multi', 'freecad-viewport', 'freecad-screen-content', 'freecad-sampling-policy', 'freecad-overlays', 'freecad-legacy-polyline', 'freecad-flags', 'freecad-grid', 'freecad-path-selection', 'freecad-mouse-picking', 'freecad-mouse-elements', 'freecad-mouse-links', 'freecad-mouse-link-topology', 'freecad-selection-menu', 'freecad-delayed-overlays'):
                        macro = {'freecad-viewport': 'freecad_viewport.FCMacro', 'hover': 'freecad_hover.FCMacro', 'freecad-multi': 'freecad_multi.FCMacro',
                                 'freecad-overlays': 'freecad_overlays.FCMacro',
                                 'freecad-screen-content': 'freecad_screen_content.FCMacro',
                                 'freecad-sampling-policy': 'freecad_sampling_policy.FCMacro',
                                 'freecad-legacy-polyline': 'freecad_legacy_polyline.FCMacro',
                                 'freecad-flags': 'freecad_flags.FCMacro',
                                 'freecad-delayed-overlays': 'freecad_delayed_overlays.FCMacro',
                                 'freecad-grid': 'freecad_grid.FCMacro',
                                 'freecad-path-selection': 'freecad_path_selection.FCMacro',
                                 'freecad-mouse-picking': 'freecad_mouse_picking.FCMacro',
                                 'freecad-mouse-elements': 'freecad_mouse_elements.FCMacro',
                                 'freecad-mouse-links': 'freecad_mouse_links.FCMacro',
                                 'freecad-mouse-link-topology': 'freecad_mouse_link_topology.FCMacro',
                                 'freecad-selection-menu': 'freecad_selection_menu.FCMacro'}[case]
                        profile = directory / 'private-profile'
                        prepare_profile(profile, case)
                        command = [str(args.freecad.resolve()),
                                   '--user-cfg', str(profile / 'user.cfg'),
                                   '--system-cfg', str(profile / 'system.cfg'),
                                   str(Path(__file__).with_name(macro))]
                    if case == 'freecad-delayed-overlays':
                        # No viewport/document or desktop capture: the helper renders
                        # the real Gui nodes to an offscreen BGFX hardware target.
                        env.update(FREECAD_COIN_WGPU='0', QT_QPA_PLATFORM='offscreen')
                    try:
                        code, output = execute(command, env, args.timeout)
                        result = classify(code, output, require_bgfx=not args.reference_gl,
                                          backend=args.backend, offscreen=case in OFFSCREEN_CASES)
                        if args.reference_gl and result['status'] == 'PASS':
                            if result.get('reference_gl') is True:
                                result['status'] = 'REFERENCE_PASS'
                            else:
                                result.update(status='FAIL', reason='reference macro did not identify Coin/GL')
                        if case in ('hover', 'freecad-multi', 'freecad-viewport', 'freecad-screen-content', 'freecad-sampling-policy', 'freecad-overlays', 'freecad-legacy-polyline', 'freecad-flags', 'freecad-grid', 'freecad-path-selection', 'freecad-mouse-picking', 'freecad-mouse-elements', 'freecad-mouse-links', 'freecad-mouse-link-topology', 'freecad-selection-menu') and code in (0, 1) and session_locked() is True:
                            result = dict(status='SKIP', reason='session locked during screen capture',
                                          captured_result=result)
                        if 'HARNESS_TIMEOUT' in output:
                            result = dict(status='FAIL', reason=f'timeout {args.timeout}s; possible infinite frame loop')
                    except OSError as error:
                        output = str(error)
                        result = dict(status='FAIL', reason='test executable could not start')
                (directory / 'process.log').write_text(output)
                offscreen = case in OFFSCREEN_CASES
                devices = device_evidence(output, args.backend, offscreen)
                if result['status'] == 'PASS' and (not devices or any(d[0] != renderer for d in devices)):
                    result.update(status='FAIL', reason='requested renderer was not proven; fallback is forbidden')
                if devices and not result.get('adapter'):
                    actual_renderer, vendor, device = devices[-1]
                    result['adapter'] = f'{args.backend} {actual_renderer} vendor {vendor} device {device}'
                adapter = str(result.get('adapter', '')).lower()
                actual_software = any(word in adapter for word in ('llvmpipe', 'lavapipe', 'softpipe', 'cpu')) or (renderer == 'opengl' and software)
                known_vendor = bool(devices) and all(d[1] in ('0x1002', '0x10de', '0x8086', '0x13b5', '0x5143') for d in devices)
                hardware = not actual_software and bool(devices) and (known_vendor or
                            (renderer == 'opengl' and 'accelerated: yes' in description))
                if args.backend == 'bgfx' and renderer == 'opengl' and sys.platform.startswith('linux'):
                    gl_adapters, physical = bgfx_gl_adapter_evidence(output)
                    # The backend render context, rather than Qt GLX or process
                    # residency, identifies the selected device/provider.
                    hardware = bool(devices) and physical
                    result['backend_gl_contexts'] = gl_adapters
                if args.backend == 'wgpu':
                    phase = 'wgpu_offscreen' if offscreen else 'wgpu_surface'
                    types = re.findall(r'COIN_RENDER_PHASE ' + phase + r' .*?device_type=(\w+)', output)
                    hardware = bool(types) and all(t in ('IntegratedGpu', 'DiscreteGpu') for t in types)
                    if renderer == 'opengl':
                        names, hardware = wgpu_gl_adapter_evidence(output)
                        result['backend_gl_contexts'] = names
                submitted = submission_evidence(output, args.backend, offscreen)
                result.update(test=case, backend=args.backend, renderer=renderer, mode=mode, alpha=alpha,
                              scale=scale, hardware_gpu=hardware and submitted,
                              gpu_submitted=submitted,
                              gpu_execution=('hardware' if hardware else 'software-or-unverified') if submitted else 'not-submitted', artifacts=str(directory))
                if args.require_hardware and result['status'] == 'PASS' and not hardware:
                    result.update(status='FAIL', reason='physical GPU execution was not proven')
                (directory / 'result.json').write_text(json.dumps(result, indent=2))
                results.append(result)
                # Preserve completed cells if a long matrix is interrupted.
                checkpoint_results(args.artifacts, results)
                print(f"{result['status']:11} {directory.name}: {result.get('reason', '')}", flush=True)
    checkpoint_results(args.artifacts, results)
    counts = {status: sum(r['status'] == status for r in results) for status in EXIT}
    print(json.dumps(counts))
    return next((EXIT[status] for status in ('FAIL', 'SKIP', 'UNSUPPORTED') if counts[status]), 0)

if __name__ == '__main__':
    sys.exit(main())
