#!/usr/bin/env python3
"""Own a private authenticated X server, WM and D-Bus for native UI tests."""
import argparse
import contextlib
import json
import os
import re
from pathlib import Path
import secrets
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from run import window_manager_available


def stop(process):
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            return
        process.wait(timeout=5)


def private_mount_present(path):
    # stat/ismount can report false for a disconnected FUSE endpoint (ENOTCONN).
    # Inspect kernel mount ownership instead, restricted to our exact temp path.
    return any(line.split()[4] == str(path)
               for line in Path('/proc/self/mountinfo').read_text().splitlines())


def screen_dimensions(output):
    match = re.search(r'dimensions:\s+(\d+x\d+)\s+pixels', output)
    if not match:
        raise RuntimeError('private X server did not report its actual dimensions')
    return match.group(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', choices=('xvfb', 'xephyr', 'xwayland'), default='xvfb')
    parser.add_argument('--weston-prefix', type=Path,
                        help='optional locally extracted Weston prefix (xwayland only)')
    parser.add_argument('--weston-shell', choices=('kiosk-shell.so', 'desktop-shell.so'),
                        default='kiosk-shell.so', help='Weston shell for xwayland sessions')
    parser.add_argument('--artifacts', required=True, type=Path)
    parser.add_argument('--private-dir', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--worker', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--exec', action='store_true', help='run a supplied command in the private session')
    argv = sys.argv[1:]
    if '--exec' in argv:
        if '--' not in argv:
            parser.error('--exec requires -- before the command')
        separator = argv.index('--')
        args = parser.parse_args(argv[:separator])
        runner_args = argv[separator + 1:]
    else:
        args, runner_args = parser.parse_known_args(argv)
    if not args.worker:
        # Keep temporary runtime alive until the private D-Bus daemon and its
        # services have exited. GIO may mount its own FUSE directory there.
        with tempfile.TemporaryDirectory(prefix='coin-isolated-x-') as private:
            code = subprocess.call(['dbus-run-session', '--', sys.executable,
                                    str(Path(__file__).resolve()), '--worker',
                                    '--private-dir', private, *sys.argv[1:]])
            mount = Path(private)/'runtime/gvfs'
            for _ in range(50):
                if not private_mount_present(mount):
                    break
                time.sleep(.1)
            if private_mount_present(mount):
                subprocess.run(['fusermount3', '-u', str(mount)], check=True)
            return code
    if args.private_dir is None:
        parser.error('--worker requires its parent-owned private directory')

    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    server = shutil.which({'xvfb': 'Xvfb', 'xephyr': 'Xephyr',
                           'xwayland': 'Xwayland'}[args.server])
    wm = shutil.which('metacity')
    if not server or not wm or not shutil.which('xauth'):
        print('Missing X server, metacity or xauth', file=sys.stderr)
        return 78
    xserver = manager = compositor = None
    with contextlib.nullcontext(str(args.private_dir)) as private:
        runtime = Path(private) / 'runtime'
        runtime.mkdir(mode=0o700)
        xdg = {}
        for key, name in (('XDG_DATA_HOME', 'data'), ('XDG_CONFIG_HOME', 'config'),
                          ('XDG_CACHE_HOME', 'cache')):
            directory = Path(private)/name
            directory.mkdir(mode=0o700)
            xdg[key] = str(directory)
        auth = Path(private) / 'Xauthority'
        cookie = secrets.token_hex(16)
        # Server-side authorization reads this protocol/cookie irrespective of
        # the entry's display name. Add the discovered client display below.
        subprocess.run(['xauth', '-f', str(auth), 'add', ':0', '.', cookie],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        server_env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime),
                          GSETTINGS_BACKEND='memory')
        if args.server == 'xwayland':
            weston = shutil.which('weston')
            if args.weston_prefix:
                prefix = args.weston_prefix.resolve()
                weston = str(prefix / 'usr/bin/weston')
                libs = prefix / 'usr/lib/x86_64-linux-gnu'
                server_env['LD_LIBRARY_PATH'] = str(libs)+':'+str(libs/'weston')
                modules = list((libs/'libweston-13').glob('*.so'))
                modules += list((libs/'weston').glob('*.so'))
                server_env['WESTON_MODULE_MAP'] = ';'.join(
                    module.name+'='+str(module) for module in modules)
                server_env['WESTON_DATA_DIR'] = str(prefix/'usr/share/weston')
            if not weston:
                print('Weston is required for isolated accelerated Xwayland', file=sys.stderr)
                return 78
            server_env['WAYLAND_DISPLAY'] = 'coin-isolated'
            weston_config = ['--no-config']
            if args.weston_shell == 'desktop-shell.so' and args.weston_prefix:
                config = artifacts/'weston.ini'
                config.write_text('[shell]\nclient='+str(prefix/'usr/libexec/weston-desktop-shell')+'\n')
                weston_config = ['--config='+str(config)]
            with (artifacts/'weston-process.log').open('w') as log:
                compositor = subprocess.Popen([
                    weston, '--backend=headless', '--renderer=gl',
                    '--shell='+args.weston_shell, '--socket=coin-isolated',
                    '--width=2400', '--height=1600', '--idle-time=0',
                    *weston_config, '--log='+str(artifacts/'weston.log')],
                    env=server_env, stdout=log, stderr=subprocess.STDOUT,
                    start_new_session=True)
            deadline = time.monotonic()+15
            while not (runtime/'coin-isolated').exists():
                if compositor.poll() is not None or time.monotonic()>=deadline:
                    stop(compositor)
                    raise RuntimeError('private Weston failed; inspect weston logs')
                time.sleep(.1)
        read_fd, write_fd = os.pipe()
        try:
            with (artifacts / 'xserver.log').open('w') as log:
                options = (['-screen', '0', '2400x1600x24'] if args.server == 'xvfb'
                           else ['-screen', '2400x1600', '-glamor', '-no-host-grab',
                                 '-title', 'Coin isolated GPU tests'] if args.server == 'xephyr'
                           else ['-geometry', '2400x1600'])
                xserver = subprocess.Popen(
                    [server, '-displayfd', str(write_fd), '-auth', str(auth),
                     '-nolisten', 'tcp', *options],
                    env=server_env, pass_fds=(write_fd,), stdout=log, stderr=subprocess.STDOUT,
                    start_new_session=True)
            os.close(write_fd)
            write_fd = None
            if not select.select([read_fd], [], [], 15)[0]:
                raise RuntimeError('private X server did not become ready')
            number = os.read(read_fd, 64).decode().strip()
            if not number.isdecimal():
                raise RuntimeError('private X server failed; inspect xserver.log')
            display = ':' + number
            subprocess.run(['xauth', '-f', str(auth), 'add', display, '.', cookie],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            env = dict(os.environ, COIN_TEST_ISOLATED_X11='1', DISPLAY=display, XAUTHORITY=str(auth),
                       GSETTINGS_BACKEND='memory', XDG_RUNTIME_DIR=str(runtime),
                       QT_QPA_PLATFORMTHEME='generic', **xdg)
            # Activation services must inherit the PRIVATE display, not the
            # caller's desktop. Do not ask D-Bus to update systemd/user settings.
            subprocess.run(['dbus-update-activation-environment', 'DISPLAY',
                            'XAUTHORITY', 'GSETTINGS_BACKEND', 'XDG_RUNTIME_DIR',
                            'QT_QPA_PLATFORMTHEME', 'XDG_DATA_HOME',
                            'XDG_CONFIG_HOME', 'XDG_CACHE_HOME'], env=env, check=True)
            with (artifacts / 'window-manager.log').open('w') as log:
                manager = subprocess.Popen([wm, '--sm-disable', '--no-composite'],
                                           env=env, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
            deadline = time.monotonic()+15
            while time.monotonic() < deadline:
                probe = subprocess.run(['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'],
                                       env=env, text=True, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, timeout=2)
                if window_manager_available(probe.stdout):
                    break
                if manager.poll() is not None:
                    raise RuntimeError('private WM exited; inspect window-manager.log')
                time.sleep(.1)
            else:
                raise RuntimeError('private WM did not publish EWMH readiness')
            screen_probe = subprocess.run(['xdpyinfo'], env=env, text=True,
                                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          check=True, timeout=5)
            screen = screen_dimensions(screen_probe.stdout)
            randr = subprocess.run(['xrandr', '--current'], env=env, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
            (artifacts / 'xrandr-before.log').write_text(randr.stdout)
            width, height = map(int, screen.split('x'))
            if width < 2400 or height < 1600:
                raise RuntimeError('private X server screen is smaller than required: ' + screen)
            (artifacts / 'screen.log').write_text(screen_probe.stdout)
            (artifacts / 'xrandr.log').write_text(randr.stdout)
            (artifacts / 'isolated-session.json').write_text(json.dumps({
                'server': args.server, 'display': display,
                'private_dbus': True, 'authenticated': True,
                'host_input_forwarded': args.server == 'xephyr',
                'window_manager': wm, 'screen': screen, 'requested_screen': '2400x1600',
                'private_runtime': True,
                'compositor_backend': 'headless-gl' if args.server == 'xwayland' else None,
                'compositor_shell': args.weston_shell if args.server == 'xwayland' else None,
                'hardware': 'must be proven by runner, never inferred from isolation',
            }, indent=2))
            if args.exec:
                if not runner_args:
                    parser.error('--exec requires an executable and arguments')
                return subprocess.call(runner_args, env=env)
            return subprocess.call([sys.executable, str(Path(__file__).with_name('run.py')),
                                    '--artifacts', str(artifacts), *runner_args], env=env)
        finally:
            os.close(read_fd)
            if write_fd is not None:
                os.close(write_fd)
            stop(manager)
            stop(xserver)
            stop(compositor)


if __name__ == '__main__':
    sys.exit(main())
