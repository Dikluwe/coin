import json, os, pathlib, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parent
SOURCE = pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\coin-render-windows-20261007\coin')
PREFIX = pathlib.Path(r'H:\Git\coin\build\bgfx-windows-install')
ENV = {k.upper(): v for k, v in os.environ.items()}
ENV['MSBUILDDISABLENODEREUSE'] = '1'
ROOT.mkdir(parents=True, exist_ok=True)

def run(name, command):
    print(time.strftime('%H:%M:%S'), name, 'started', flush=True)
    started = time.time()
    with (ROOT / (name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run(command, env=ENV, stdout=log, stderr=subprocess.STDOUT)
    print(time.strftime('%H:%M:%S'), name, 'exit', result.returncode,
          'seconds', round(time.time() - started), flush=True)
    if result.returncode:
        print('\n'.join((ROOT / (name + '.log')).read_text(errors='replace').splitlines()[-35:]), flush=True)
        raise SystemExit(result.returncode)

sha = subprocess.check_output(['git', '-C', str(SOURCE), 'rev-parse', 'HEAD'], env=ENV, text=True).strip()
(ROOT / 'source-sha.txt').write_text(sha + '\n')
for backend, mode in [('bgfx', 'BGFX'), ('wgpu', 'RUST_BRIDGE')]:
    destination = ROOT / backend
    configure = ['cmake', '-S', str(SOURCE), '-B', str(destination),
        '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DCOIN_BUILD_RENDER=ON', '-DCOIN_BUILD_TESTS=ON',
        '-DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON', '-DCOIN_BUILD_LEGACY_GL_RENDERER=ON',
        '-DCOIN_BUILD_MSVC_MP=OFF', '-DCOIN_BUILD_AWESOME_DOCUMENTATION=OFF',
        '-DCOIN_BUILD_DOCUMENTATION=OFF', '-DCOIN_RENDER_BACKEND=' + mode]
    if backend == 'bgfx':
        configure += ['-DCMAKE_PREFIX_PATH=' + str(PREFIX),
            '-DCOIN_BGFX_SHADERC_EXECUTABLE=' + str(PREFIX / 'bin/shaderc.exe'),
            '-DCOIN_BGFX_SHADER_INCLUDE_DIR=' + str(PREFIX / 'include/bgfx')]
    run(backend + '-configure', configure)
    run(backend + '-build', ['cmake', '--build', str(destination), '--config', 'Release', '--parallel', '2'])
