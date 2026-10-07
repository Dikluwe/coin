import argparse, hashlib, json, os, pathlib, shutil, subprocess, time, xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--phase', choices=['baseline', 'cross', 'smoke'], required=True)
args = parser.parse_args()
base = {k.upper(): v for k, v in os.environ.items()}
for key in list(base):
    if key.startswith(('COIN_', 'WGPU_')):
        del base[key]
base.update({key: '1' for key in [
    'COIN_RENDER_REQUIRE_GL_REFERENCE', 'COIN_WGPU_REQUIRE_GL_REFERENCE',
    'COIN_RENDER_REQUIRE_CAMERA_REFERENCE', 'COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU',
    'COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU']})
base['MSBUILDDISABLENODEREUSE'] = '1'
records = []

def execute(label, command, env):
    print(time.strftime('%H:%M:%S'), label, 'started', flush=True)
    start = time.time()
    with (ROOT / (label + '.log')).open('w', encoding='utf-8') as log:
        proc = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    if command[0] == 'ctest':
        testdir = pathlib.Path(command[command.index('--test-dir') + 1])
        last_test = testdir / 'Testing/Temporary/LastTest.log'
        if last_test.exists():
            shutil.copyfile(last_test, ROOT / (label + '-last-test.log'))
    record = dict(label=label, exit=proc.returncode, seconds=round(time.time()-start),
                  command=command, environment={k:v for k,v in env.items() if k.startswith(('COIN_', 'WGPU_'))})
    junit = ROOT / (label + '.xml')
    if junit.exists():
        cases = ET.parse(junit).findall('.//testcase')
        record.update(total=len(cases), failed=[c.attrib['name'] for c in cases if c.find('failure') is not None],
                      skipped=[c.attrib['name'] for c in cases if c.find('skipped') is not None])
    records.append(record)
    (ROOT / (args.phase + '-results.json')).write_text(json.dumps(records, indent=2))
    print(time.strftime('%H:%M:%S'), json.dumps(record), flush=True)
    print('\n'.join((ROOT / (label + '.log')).read_text(errors='replace').splitlines()[-12:]), flush=True)

for backend, apis in [('bgfx', ['d3d12', 'vulkan', 'opengl']), ('wgpu', ['dx12', 'vulkan', 'gl'])]:
    build = ROOT / backend
    inventory = json.loads(subprocess.check_output(['ctest', '--test-dir', str(build), '-C', 'Release', '--show-only=json-v1'], env=base, text=True))
    (ROOT / (backend + '-inventory.json')).write_text(json.dumps(inventory, indent=2))
    if args.phase == 'smoke':
        for api, flag in [('d3d12', []), ('vulkan', ['--vulkan']), ('opengl', ['--opengl'])]:
            exe = next(build.rglob('coin_render_win32_smoke.exe'))
            label = backend + '-win32-' + api
            if backend == 'wgpu' and api == 'opengl':
                flag += ['--expect-no-window-readback']
                label += '-presentation'
            execute(label, [str(exe)] + flag, base)
        if backend == 'bgfx':
            instancing_env = dict(base)
            instancing_env['COIN_BGFX_RENDERER'] = 'd3d12'
            execute('bgfx-instancing-d3d12',
                    [str(next(build.rglob('CoinBgfxInstancingTest.exe')))], instancing_env)
        continue
    for api in apis[:1] if args.phase == 'baseline' else apis[1:]:
        env = dict(base)
        env['COIN_BGFX_RENDERER' if backend == 'bgfx' else 'WGPU_BACKEND'] = api
        label = args.phase + '-' + backend + '-' + api
        command = ['ctest', '--test-dir', str(build), '-C', 'Release', '--parallel', '1',
                   '--output-on-failure', '--output-junit', str(ROOT / (label + '.xml'))]
        if args.phase == 'cross':
            selected = []
            for test in inventory['tests']:
                if not test['name'].startswith(('CoinRender', 'CoinBgfx', 'CoinWgpu')):
                    continue
                if 'Win32' in test['name'] or 'win32' in test['name']:
                    continue
                properties = {p['name']: p['value'] for p in test.get('properties', [])}
                overrides = properties.get('ENVIRONMENT', [])
                key = 'COIN_BGFX_RENDERER' if backend == 'bgfx' else 'WGPU_BACKEND'
                forced = [x.split('=', 1)[1] for x in overrides if x.startswith(key + '=')]
                # Some CTest cases use cmake -E env instead of ENVIRONMENT.
                forced += [x.split('=', 1)[1] for x in test.get('command', []) if x.startswith(key + '=')]
                # Explicit API variants already ran in the full baseline.
                # Repeat only cases whose renderer inherits this process's API.
                if forced:
                    continue
                selected.append(test['name'])
            selection = ROOT / (label + '-selected.txt')
            selection.write_text('\n'.join(selected) + '\n')
            command += ['--tests-from-file', str(selection)]
        execute(label, command, env)
