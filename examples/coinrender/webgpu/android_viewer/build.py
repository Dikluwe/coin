#!/usr/bin/env python3
"""Build an isolated Android TV WebView viewer with a single host-scoped public CA."""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit, urlunsplit
from xml.sax.saxutils import escape
import zipfile


def ram_directory(value):
    path = Path(value).expanduser().resolve()
    if path == Path('/dev/shm') or not path.is_relative_to(Path('/dev/shm')):
        raise argparse.ArgumentTypeError('--build-dir must be a dedicated directory inside /dev/shm')
    return path


def fixed_url(value):
    if any(ord(char) <= 32 or ord(char) == 127 for char in value) or '\\' in value:
        raise ValueError('URL must not contain whitespace, control characters or backslashes')
    parts = urlsplit(value)
    if parts.scheme != 'https' or not parts.hostname or parts.username is not None or parts.password is not None:
        raise ValueError('URL must be HTTPS with a host and no credentials')
    host = parts.hostname.encode('idna').decode('ascii').lower()
    try:
        host = ipaddress.ip_address(host).compressed
    except ValueError:
        if len(host) > 253 or not all(re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', label)
                                     for label in host.split('.')):
            raise ValueError('Invalid URL hostname')
    port = parts.port if parts.port is not None else 443
    if port < 1 or port > 65535:
        raise ValueError('Invalid HTTPS port')
    authority = '[' + host + ']' if ':' in host else host
    if port != 443:
        authority += ':' + str(port)
    return urlunsplit(('https', authority, parts.path or '/', parts.query, parts.fragment)), host, port


def latest(directory, pattern):
    matches = []
    for path in directory.iterdir():
        match = re.fullmatch(pattern, path.name)
        if path.is_dir() and match:
            matches.append((tuple(int(part) for part in match.group(1).split('.')), path))
    if not matches:
        raise RuntimeError('No compatible Android SDK component in ' + str(directory))
    return max(matches)[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--build-dir', required=True, type=ram_directory)
    parser.add_argument('--output-apk', required=True, type=Path)
    parser.add_argument('--ca', required=True, type=Path, help='One public PEM/DER X.509 CA certificate; no private key')
    parser.add_argument('--url', default='https://192.168.0.16:8443/')
    parser.add_argument('--debug', action='store_true', help='Produce a debuggable package; enables WebView remote debugging')
    args = parser.parse_args()
    url, host, port = fixed_url(args.url)
    source = Path(__file__).resolve().parent
    sdk = args.sdk.expanduser().resolve()
    platform = latest(sdk / 'platforms', r'android-([0-9]+(?:\.[0-9]+)*)')
    android_jar = platform / 'android.jar'
    tools = latest(sdk / 'build-tools', r'([0-9]+(?:\.[0-9]+)*)')
    for tool in ('aapt2', 'd8', 'zipalign', 'apksigner'):
        if not (tools / tool).is_file():
            raise RuntimeError('SDK tool missing: ' + str(tools / tool))
    for tool in ('javac', 'java', 'keytool'):
        if shutil.which(tool) is None:
            raise RuntimeError('JDK tool missing: ' + tool)
    if not android_jar.is_file():
        raise RuntimeError('Missing android.jar')
    build = args.build_dir
    build.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='viewer-', dir=build))
    tmp = work / 'tmp'
    tmp.mkdir()
    env = os.environ.copy()
    env.update(TMPDIR=str(tmp), TMP=str(tmp), TEMP=str(tmp), XDG_CACHE_HOME=str(work / 'cache'),
               PYTHONDONTWRITEBYTECODE='1')
    # These SDK launchers use Java; keep VM temporary state in RAM as well.
    env['JAVA_TOOL_OPTIONS'] = '-Djava.io.tmpdir="' + str(tmp) + '"'
    def run(command):
        print('Running ' + Path(str(command[0])).name, flush=True)
        subprocess.run([str(arg) for arg in command], check=True, env=env)
    resources = work / 'res'
    (resources / 'raw').mkdir(parents=True)
    (resources / 'xml').mkdir()
    validator = work / 'validator-classes'
    validator.mkdir()
    run(['javac', '-encoding', 'UTF-8', '-d', validator, source / 'ValidateCa.java'])
    run(['java', '-cp', validator, 'ValidateCa', args.ca.expanduser().resolve(), resources / 'raw/viewer_ca.der'])
    # Empty base trust anchors deny other hosts. The exact domain has only the
    # supplied CA, without system/user anchors or a debug trust override.
    (resources / 'xml/network_security_config.xml').write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<network-security-config>\n'
        '  <base-config cleartextTrafficPermitted="false"><trust-anchors /></base-config>\n'
        '  <domain-config cleartextTrafficPermitted="false">\n'
        '    <domain includeSubdomains="false">' + escape(host) + '</domain>\n'
        '    <trust-anchors><certificates src="@raw/viewer_ca" /></trust-anchors>\n'
        '  </domain-config>\n'
        '</network-security-config>\n', encoding='utf-8')
    manifest = work / 'AndroidManifest.xml'
    manifest.write_text((source / 'AndroidManifest.xml.in').read_text().replace(
        '@DEBUGGABLE@', 'true' if args.debug else 'false'), encoding='utf-8')
    generated = work / 'generated'
    generated.mkdir()
    (generated / 'ViewerConfig.java').write_text(
        'package org.coin3d.coinrender.webgpuviewer;\n'
        'final class ViewerConfig {\n'
        '  static final String URL = ' + json.dumps(url) + ';\n'
        '  static final String HOST = ' + json.dumps(host) + ';\n'
        '  static final int PORT = ' + str(port) + ';\n'
        '}\n', encoding='utf-8')
    resource_zip = work / 'resources.zip'
    unsigned = work / 'unsigned.apk'
    run([tools / 'aapt2', 'compile', '--dir', resources, '-o', resource_zip])
    run([tools / 'aapt2', 'link', '-o', unsigned, '--manifest', manifest, '-I', android_jar,
         '--java', generated, '--min-sdk-version', '23', '--target-sdk-version', '30', resource_zip])
    classes = work / 'classes'
    classes.mkdir()
    java_files = sorted((source / 'src').rglob('*.java')) + sorted(generated.rglob('*.java'))
    run(['javac', '-encoding', 'UTF-8', '-source', '8', '-target', '8', '-classpath', android_jar,
         '-d', classes] + java_files)
    dex = work / 'dex'
    dex.mkdir()
    run([tools / 'd8', '--min-api', '23', '--lib', android_jar, '--output', dex]
        + sorted(classes.rglob('*.class')))
    with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as apk:
        for file in sorted(dex.glob('classes*.dex')):
            apk.write(file, file.name)
    # Reuse the task's signing identity so wrong-CA and valid-CA diagnostic
    # builds can replace this package. Both private key and password stay in RAM.
    signing = build / 'signing'
    signing.mkdir(mode=0o700, exist_ok=True)
    keystore, password = signing / 'viewer.p12', signing / 'password.txt'
    if keystore.exists() != password.exists():
        raise RuntimeError('Incomplete RAM signing identity; use another dedicated build directory')
    if not keystore.exists():
        password.write_text(secrets.token_urlsafe(32), encoding='ascii')
        password.chmod(0o600)
        run(['keytool', '-genkeypair', '-noprompt', '-keystore', keystore, '-storetype', 'PKCS12',
             '-storepass:file', password, '-keypass:file', password, '-alias', 'viewer',
             '-keyalg', 'RSA', '-keysize', '2048', '-validity', '30', '-dname', 'CN=CoinRender Diagnostic Viewer'])
        keystore.chmod(0o600)
    aligned, signed = work / 'aligned.apk', work / 'viewer.apk'
    run([tools / 'zipalign', '-f', '4', unsigned, aligned])
    run([tools / 'apksigner', 'sign', '--ks', keystore, '--ks-key-alias', 'viewer',
         '--ks-pass', 'file:' + str(password), '--out', signed, aligned])
    run([tools / 'apksigner', 'verify', signed])
    output = args.output_apk.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(signed, output)
    print('Diagnostic APK: ' + str(output))
    print('Fixed HTTPS origin: ' + host + ':' + str(port) + '; debug=' + str(args.debug))
    print('A successful build does not establish WebGPU availability; inspect CoinRenderViewer logcat.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit('Viewer build failed: ' + str(error))
