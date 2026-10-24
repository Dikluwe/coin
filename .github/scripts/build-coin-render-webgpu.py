#!/usr/bin/env python3
"""Build the isolated CoinRender C++ capture + Rust wgpu browser demonstration."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def ram_path(value):
    path = Path(value).expanduser().resolve()
    if not path.is_relative_to(Path('/dev/shm')) or path == Path('/dev/shm'):
        raise argparse.ArgumentTypeError('Disposable build/output paths must be inside a task directory in /dev/shm')
    return path


def executable(value):
    found = shutil.which(value)
    if not found:
        raise RuntimeError(f'Required tool not found: {value}')
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=ram_path)
    parser.add_argument('--output-dir', required=True, type=ram_path)
    parser.add_argument('--emcmake', default='emcmake')
    parser.add_argument('--cargo', default='cargo')
    parser.add_argument('--wasm-bindgen', default='wasm-bindgen')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 8))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    root = Path(__file__).resolve().parents[2]
    source = root / 'examples/coinrender/webgpu'
    build, output = args.build_dir, args.output_dir
    if output == build or build.is_relative_to(output):
        parser.error('--output-dir must not contain --build-dir')
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for name, default in [('TMPDIR', build / 'tmp'), ('CARGO_TARGET_DIR', build / 'cargo-target'),
                          ('CARGO_HOME', build / 'cargo-home'), ('EM_CACHE', build / 'em-cache')]:
        # An explicitly provisioned RAM cache can be reused across invocations.
        chosen = ram_path(env.get(name, str(default)))
        chosen.mkdir(parents=True, exist_ok=True)
        env[name] = str(chosen)
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    emcmake, cargo, bindgen = map(executable, [args.emcmake, args.cargo, args.wasm_bindgen])
    def run(command):
        print('+', ' '.join(map(str, command)), flush=True)
        subprocess.run(list(map(str, command)), check=True, env=env)
    run([emcmake, 'cmake', '-S', source, '-B', build / 'cpp', '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DCOIN_BUILD_SHARED_LIBS=OFF', '-DCOIN_BUILD_TESTS=OFF',
         '-DCOIN_BUILD_EXAMPLES=OFF', '-DCOIN_BUILD_DOCUMENTATION=OFF', '-DCOIN_USE_CPACK=OFF',
         '-DCOIN_BUILD_RENDER=ON', '-DCOIN_RENDER_BACKEND=RECORDING',
         '-DCOIN_BUILD_LEGACY_GL_RENDERER=OFF', '-DCOIN_BUILD_GLX=OFF', '-DCOIN_BUILD_EGL=OFF',
         '-DHAVE_SOUND=OFF', '-DCOIN_HAVE_JAVASCRIPT=OFF'])
    run(['cmake', '--build', build / 'cpp', '--target', 'coin_capture', '-j', args.jobs])
    # Keep Cargo's generated lockfile and all compiler state in RAM too.
    rust_source = build / 'rust-source'
    rust_source.mkdir(exist_ok=True)
    shutil.copy2(source / 'renderer/Cargo.toml', rust_source / 'Cargo.toml')
    shutil.copytree(source / 'renderer/src', rust_source / 'src', dirs_exist_ok=True)
    run([cargo, 'build', '--manifest-path', rust_source / 'Cargo.toml', '--target',
         'wasm32-unknown-unknown', '--release', '-j', args.jobs])
    run([bindgen, Path(env['CARGO_TARGET_DIR']) / 'wasm32-unknown-unknown/release/coinrender_webgpu_demo.wasm',
         '--target', 'web', '--out-dir', output, '--out-name', 'renderer', '--no-typescript'])
    for name in ('coin_capture.js', 'coin_capture.wasm'):
        shutil.copy2(build / 'cpp' / name, output / name)
    for name in ('index.html', 'app.js'):
        shutil.copy2(source / name, output / name)
    for name, grid in [('city-million.bin', 1000), ('city-40000.bin', 200)]:
        run([sys.executable, source / 'generate_city_asset.py', output / name, '--grid', grid, '--seed', 136])
    print(f'Artifacts built in {output}. Serve this directory with examples/coinrender/webgpu/serve.py over HTTPS.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f'WebGPU build failed: {error}')
