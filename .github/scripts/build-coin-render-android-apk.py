#!/usr/bin/env python3
"""Build and sign the P23 NativeActivity development APK (no Gradle required)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--ndk', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--abi', choices=['x86_64', 'arm64-v8a'], default='x86_64')
    parser.add_argument('--renderer', choices=['opengl', 'vulkan'], default='opengl')
    parser.add_argument('--build-tools', default='36.0.0')
    parser.add_argument('--platform', default='android-37.0')
    parser.add_argument('--target-sdk', type=int, default=37)
    parser.add_argument('--java-home', type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--include-million-city', action='store_true')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    sdk, ndk = args.sdk.resolve(), args.ndk.resolve()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    if str(build).startswith('/tmp/') or str(output).startswith('/tmp/'):
        parser.error('Use permanent build/output directories for this campaign')
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if args.java_home:
        env['JAVA_HOME'] = str(args.java_home.resolve())
        env['PATH'] = str(args.java_home.resolve() / 'bin') + os.pathsep + env.get('PATH', '')
    tools = sdk / 'build-tools' / args.build_tools
    host = 'linux-x86_64'
    llvm = ndk / 'toolchains/llvm/prebuilt' / host
    commands = []

    def run(name, command):
        command = [str(x) for x in command]
        with (output / (name + '.log')).open('w') as log:
            log.write(json.dumps(command) + '\n')
            log.flush()
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        commands.append({'step': name, 'command': command, 'exit': result.returncode})
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(f'{name} failed; see {output / (name + ".log")}')

    run('configure', ['cmake', '-S', repo, '-B', build, '-G', 'Ninja',
        '-DCMAKE_TOOLCHAIN_FILE=' + str(ndk / 'build/cmake/android.toolchain.cmake'),
        '-DANDROID_NDK=' + str(ndk), '-DANDROID_ABI=' + args.abi,
        '-DANDROID_PLATFORM=android-26', '-DANDROID_STL=c++_shared',
        '-DCMAKE_BUILD_TYPE=Release', '-DCOIN_BUILD_RENDER=ON',
        '-DCOIN_RENDER_BACKEND=RUST_BRIDGE', '-DCOIN_BUILD_RENDER_WINDOW_EXAMPLE=ON',
        '-DCOIN_BUILD_LEGACY_GL_RENDERER=OFF',
        '-DCOIN_RENDER_ANDROID_SMOKE_RENDERER=' + args.renderer.upper()])
    run('build', ['cmake', '--build', build, '--target', 'coin_render_android_smoke', '-j', args.jobs])
    staging = output / ('native-' + args.abi)
    staging.mkdir(exist_ok=True)
    triple = {'x86_64': 'x86_64-linux-android', 'arm64-v8a': 'aarch64-linux-android'}[args.abi]
    libs = [build / 'lib' / name for name in ['libCoin.so', 'libCoinRender.so', 'libcoin_render_android_smoke.so']]
    libs.append(llvm / 'sysroot/usr/lib' / triple / 'libc++_shared.so')
    hashes = {}
    for source in libs:
        dest = staging / source.name
        shutil.copy2(source, dest)
        hashes[str(source)] = hashlib.sha256(source.read_bytes()).hexdigest()
        run('strip-' + source.stem, [llvm / 'bin/llvm-strip', '--strip-unneeded', dest])
        hashes[str(dest)] = hashlib.sha256(dest.read_bytes()).hexdigest()
        run('elf-' + source.stem, [llvm / 'bin/llvm-readelf', '-h', '-l', '-d', '--dyn-syms', dest])
    city = output / 'city-40000.iv'
    run('generate-city', ['python3', repo / 'examples/coinrender/generate_large_scene.py',
        city, '--grid', '200', '--seed', '136'])
    hashes[str(city)] = hashlib.sha256(city.read_bytes()).hexdigest()
    million = output / 'city-1000000.iv' if args.include_million_city else None
    if million:
        run('generate-million-city', ['python3', repo / 'examples/coinrender/generate_large_scene.py',
            million, '--grid', '1000', '--seed', '136'])
        hashes[str(million)] = hashlib.sha256(million.read_bytes()).hexdigest()
    raw, aligned = output / 'unsigned.apk', output / 'aligned.apk'
    apk = output / ('coin-render-p23-' + args.abi + '.apk')
    run('aapt2', [tools / 'aapt2', 'link', '-o', raw, '--manifest',
        repo / 'examples/coinrender/AndroidManifest-p23.xml', '-I',
        sdk / 'platforms' / args.platform / 'android.jar',
        '--min-sdk-version', '26', '--target-sdk-version', args.target_sdk,
        '--version-code', '1', '--version-name', '0.1-p23'])
    with zipfile.ZipFile(raw, 'a', compression=zipfile.ZIP_STORED) as archive:
        archive.write(city, 'assets/city-40000.iv', compress_type=zipfile.ZIP_DEFLATED)
        if million:
            archive.write(million, 'assets/city-1000000.iv', compress_type=zipfile.ZIP_DEFLATED)
        for library in sorted(staging.glob('*.so')):
            archive.write(library, 'lib/' + args.abi + '/' + library.name)
    run('zipalign', [tools / 'zipalign', '-P', '16', '-f', '4', raw, aligned])
    keystore = build / 'p23-development.keystore'
    if not keystore.exists():
        run('keytool', ['keytool', '-genkeypair', '-keystore', keystore,
            '-storepass', 'android', '-keypass', 'android', '-alias', 'androiddebugkey',
            '-keyalg', 'RSA', '-keysize', '2048', '-validity', '3650',
            '-dname', 'CN=CoinRender P23 Development,O=Coin,C=BR', '-noprompt'])
        keystore.chmod(0o600)
    run('sign', [tools / 'apksigner', 'sign', '--ks', keystore,
        '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android',
        '--key-pass', 'pass:android', '--out', apk, aligned])
    run('verify', [tools / 'apksigner', 'verify', '--verbose', '--print-certs', apk])
    run('verify-align', [tools / 'zipalign', '-c', '-P', '16', '4', apk])
    hashes[str(apk)] = hashlib.sha256(apk.read_bytes()).hexdigest()
    (output / 'binary-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    print(apk)


if __name__ == '__main__':
    main()
