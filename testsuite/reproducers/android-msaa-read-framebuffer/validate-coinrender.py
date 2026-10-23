from pathlib import Path
import argparse, json, subprocess, time, re

parser = argparse.ArgumentParser(description='Qualify CoinRender GLES presentation and lifecycle on an emulator')
parser.add_argument('samples', type=int, choices=(1, 2, 4))
parser.add_argument('scene', choices=('cube', 'city', 'million'))
parser.add_argument('--artifacts', type=Path, required=True)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--serial', default='emulator-5554')
args = parser.parse_args()
root = args.artifacts.resolve()
samples = args.samples
scene = args.scene
out = root / ('validated-%s-%dx' % (scene, samples))
out.mkdir(exist_ok=True)
adb = str(args.sdk / 'platform-tools/adb')
serial = args.serial
pkg = 'org.coin3d.coinrender.p23'
activity = pkg + '/android.app.NativeActivity'
commands = []
pid = ''
old_rotation = None
deadline_seconds = 180 if scene == 'million' else 60
result = {'scene': scene, 'samples': samples, 'status': 'FAIL'}

def run(*args, binary=False):
    argv = [adb, '-s', serial, *args]
    r = subprocess.run(argv, capture_output=True, timeout=40)
    commands.append({'command': argv, 'exit': r.returncode})
    if r.returncode:
        raise RuntimeError(r.stderr.decode(errors='replace'))
    return r.stdout if binary else r.stdout.decode(errors='replace')

def observe(predicate):
    deadline = time.monotonic() + deadline_seconds
    text = ''
    while time.monotonic() < deadline:
        text = run('logcat', '-d', '--pid=' + pid, '-s', 'CoinRenderP23', 'AndroidRuntime', 'libc')
        (out / 'application.log').write_text(text)
        if re.search(r' E CoinRenderP23|Fatal signal|FATAL EXCEPTION', text):
            raise RuntimeError('Application error; see ' + str(out / 'application.log'))
        frames = re.findall(r'generation=(\d+) serial=(\d+) size=(\d+)x(\d+) rgba_fnv64=([0-9a-f]+) capture_scope=(\w+)', text)
        if predicate(text, frames):
            return text, frames
        if run('shell', 'pidof', pkg).strip() != pid:
            raise RuntimeError('Android process ended before the expected checkpoint')
        time.sleep(.25)
    raise RuntimeError('Checkpoint timed out; see ' + str(out / 'application.log'))

extras = ['--ei', 'coinrender_renderer', '2', '--ei', 'coinrender_samples', str(samples),
          '--ez', 'coinrender_city', 'false' if scene == 'cube' else 'true',
          '--ez', 'coinrender_million_city', 'true' if scene == 'million' else 'false',
          '--ez', 'coinrender_continuous', 'true' if scene != 'cube' else 'false']
try:
    old_rotation = run('shell', 'wm', 'user-rotation').strip()
    run('shell', 'wm', 'user-rotation', 'lock', '0')
    run('shell', 'am', 'start', '-S', '-W', '-n', activity, *extras)
    pid = run('shell', 'pidof', pkg).strip()
    result['pid'] = pid
    text, frames = observe(lambda t, f: len(f) >= (2 if scene == 'cube' else 1) and 'renderer=2 vendor=' in t)
    assert 'requested_renderer=2' in text and 'requested_samples=%d' % samples in text
    assert 'legacy_gl_unavailable=1' in text and 'emulator_noncompliant_vulkan_opt_in=0' in text
    assert '(Gl)' in text
    if scene != 'cube':
        assert 'buildings=%d' % (1000000 if scene == 'million' else 40000) in text
    if scene == 'million':
        # Initial capture precedes queued window/layout commands. Wait until
        # normal frames demonstrate that startup finished before sending HOME.
        text, frames = observe(lambda t, f: 'frames=10 ' in t)
    result['initial'] = frames[-1]
    (out / 'portrait.png').write_bytes(run('exec-out', 'screencap', '-p', binary=True))
    count = len(frames)
    run('shell', 'input', 'keyevent', 'KEYCODE_HOME')
    observe(lambda t, f: 'lifecycle command=13 ' in t)
    result['pause'] = True
    run('shell', 'am', 'start', '-W', '-n', activity, *extras)
    assert run('shell', 'pidof', pkg).strip() == pid, 'Resume unexpectedly restarted process'
    text, frames = observe(lambda t, f: len(f) > count)
    result['resume'] = frames[-1]
    count = len(frames)
    run('shell', 'wm', 'user-rotation', 'lock', '1')
    text, frames = observe(lambda t, f: len(f) > count and int(f[-1][2]) > int(f[-1][3]))
    result['landscape'] = frames[-1]
    (out / 'landscape.png').write_bytes(run('exec-out', 'screencap', '-p', binary=True))
    count = len(frames)
    run('shell', 'wm', 'user-rotation', 'lock', '0')
    text, frames = observe(lambda t, f: len(f) > count and int(f[-1][2]) < int(f[-1][3]))
    result['portrait_return'] = frames[-1]
    if scene == 'cube':
        assert len({f[4] for f in frames}) == 1, 'Cube offscreen capture changed across lifecycle'
        if samples == 1:
            assert all(f[4] == 'dc8224a1b44d3d8d' for f in frames)
    assert all(f[5] == 'offscreen' for f in frames)
    run('shell', 'input', 'keyevent', 'KEYCODE_BACK')
    text, frames = observe(lambda t, f: 'P23 smoke finished: OK' in t)
    result.update(status='PASS', checkpoints=len(frames), backend='wgpu/OpenGL ES',
                  serials=[int(f[1]) for f in frames], frames=frames)
    print(json.dumps(result), flush=True)
except Exception as error:
    result['error'] = str(error)
    print(json.dumps(result), flush=True)
    raise
finally:
    if old_rotation is not None:
        if old_rotation == 'free': run('shell', 'wm', 'user-rotation', 'free')
        elif old_rotation.startswith('lock '): run('shell', 'wm', 'user-rotation', 'lock', old_rotation.split()[-1])
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
