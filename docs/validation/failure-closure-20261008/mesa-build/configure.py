from pathlib import Path
import os,subprocess,json
root=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-failure-fixes/mesa-study')
env=dict(os.environ,CC='/usr/bin/cc',CXX='/usr/bin/c++',PKG_CONFIG='/usr/bin/pkg-config',
    PKG_CONFIG_PATH=str(root/'dependencies/usr/lib/x86_64-linux-gnu/pkgconfig')+':/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig',
    PKG_CONFIG_SYSROOT_DIR=str(root/'dependencies'),PATH=str(root/'dependencies/usr/bin')+':'+str(root/'build-tools/bin')+':/usr/bin:/bin')
cmd=[str(root/'build-tools/bin/meson'),'setup',str(root/'build'),str(root/'mesa-25.2.8'),
    '--prefix='+str(root/'install'),'-Dbuildtype=release','-Dgallium-drivers=radeonsi','-Dvulkan-drivers=amd',
    '-Dplatforms=x11','-Dglx=dri','-Degl=enabled','-Dgbm=disabled','-Dllvm=disabled','-Damd-use-llvm=false',
    '-Dgallium-va=disabled','-Dgallium-vdpau=disabled','-Dvideo-codecs=','-Dbuild-tests=false']
(root/'configure-command.json').write_text(json.dumps(dict(command=cmd,environment={k:env[k] for k in ['CC','CXX','PKG_CONFIG','PKG_CONFIG_PATH','PKG_CONFIG_SYSROOT_DIR','PATH']}),indent=2))
with (root/'configure-2.log').open('w') as f:r=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT)
print('configure',r.returncode)
