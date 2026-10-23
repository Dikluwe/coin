#!/usr/bin/env python3
"""Package the independent GLES framebuffer control for the x86_64 emulator."""
import argparse
from pathlib import Path
import subprocess
import zipfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
p.add_argument('--ndk', type=Path, required=True)
p.add_argument('--java-home', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--build-tools', default='36.0.0')
p.add_argument('--platform', default='android-37.0')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
tools = a.sdk / 'build-tools' / a.build_tools
llvm = a.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin'
source = Path(__file__).with_name('probe.cpp').resolve()

def run(*command):
    subprocess.run([str(v) for v in command], check=True)

run(llvm / 'x86_64-linux-android26-clang++', '-shared', '-fPIC', '-std=c++11',
    '-static-libstdc++', source, '-o', out / 'libprobe.so', '-landroid', '-llog', '-lEGL', '-lGLESv3')
manifest = '''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
 package="org.coin3d.coinrender.msaaprobe"><uses-sdk android:minSdkVersion="26"/>
 <application android:hasCode="false" android:debuggable="true" android:label="GLES framebuffer control">
 <activity android:name="android.app.NativeActivity" android:exported="true">
 <meta-data android:name="android.app.lib_name" android:value="probe"/>
 </activity></application></manifest>'''
(out / 'AndroidManifest.xml').write_text(manifest)
run(tools / 'aapt2', 'link', '-o', out / 'raw.apk', '--manifest', out / 'AndroidManifest.xml',
    '-I', a.sdk / 'platforms' / a.platform / 'android.jar',
    '--min-sdk-version', '26', '--target-sdk-version', '37')
with zipfile.ZipFile(out / 'raw.apk', 'a', compression=zipfile.ZIP_STORED) as z:
    z.write(out / 'libprobe.so', 'lib/x86_64/libprobe.so')
run(tools / 'zipalign', '-P', '16', '-f', '4', out / 'raw.apk', out / 'aligned.apk')
key = out / 'development.keystore'
if not key.exists():
    run(a.java_home / 'bin/keytool', '-genkeypair', '-keystore', key,
        '-storepass', 'android', '-keypass', 'android', '-alias', 'androiddebugkey',
        '-keyalg', 'RSA', '-keysize', '2048', '-validity', '3650',
        '-dname', 'CN=CoinRender GLES Control,O=Coin,C=BR', '-noprompt')
    key.chmod(0o600)
run(a.java_home / 'bin/java', '-jar', tools / 'lib/apksigner.jar', 'sign',
    '--ks', key, '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
    '--out', out / 'probe.apk', out / 'aligned.apk')
run(a.java_home / 'bin/java', '-jar', tools / 'lib/apksigner.jar', 'verify', out / 'probe.apk')
print(out / 'probe.apk')
