from pathlib import Path
import shutil,shlex,subprocess,json,hashlib,difflib,os
root=Path(__file__).parent;repo=Path('/home/dikluwe/.codex/worktrees/coin-portable-sampling/coin');old=Path('/tmp/freecad-p16-build');host=root/'freecad-host';host.mkdir(exist_ok=True);build=host/'build'
if not build.exists():shutil.copytree(old,build,symlinks=True)
for link in build.rglob('*'):
 if link.is_symlink():
  target=os.readlink(link)
  if target.startswith(str(old)):
   link.unlink();link.symlink_to(target.replace(str(old),str(build)))
source=Path('/mnt/Laranja/Git/externos/freecad-source/src/Gui/Quarter/QuarterWidget.cpp');s=source.read_text();original=s
needle='            PRIVATE(this)->wgpuadapter = new CoinRenderManagerAdapter(\n                *getSoRenderManager(), surface, size);'
replacement='            CoinRenderOptions options;\n            const char* backend = std::getenv("WGPU_BACKEND");\n            const char* bgfx = std::getenv("COIN_BGFX_RENDERER");\n            options.renderer = (backend && std::string(backend) == "gl") ||\n                (bgfx && std::string(bgfx) == "opengl") ? COIN_RENDER_RENDERER_OPENGL : COIN_RENDER_RENDERER_VULKAN;\n            options.textureSamplingPolicy = this->property("coinRenderPortableSampling").toBool() ?\n                COIN_RENDER_SAMPLING_PORTABLE : COIN_RENDER_SAMPLING_NATIVE;\n            PRIVATE(this)->wgpuadapter = new CoinRenderManagerAdapter(\n                *getSoRenderManager(), surface, size, options);\n            this->setProperty("coinRenderActiveSamplingPolicy", int(options.textureSamplingPolicy));'
assert needle in s;s=s.replace(needle,replacement)
needle='    if (PRIVATE(this)->wgpuadapter && PRIVATE(this)->wgpuwindow != windowId) {'
replacement='    if (PRIVATE(this)->wgpuadapter && (PRIVATE(this)->wgpuwindow != windowId ||\n        PRIVATE(this)->wgpuadapter->getSceneManager()->getRenderTarget()->getOptions().textureSamplingPolicy !=\n            (this->property("coinRenderPortableSampling").toBool() ? COIN_RENDER_SAMPLING_PORTABLE : COIN_RENDER_SAMPLING_NATIVE))) {'
assert needle in s;s=s.replace(needle,replacement)
# Preserve the host's existing renderer/transparency environment configuration.
# Explicit host property selects only sampling; engine selectors remain explicit.
s=s.replace('#include <Inventor/rendering/CoinRenderSceneManager.h>', '#include <Inventor/rendering/CoinRenderSceneManager.h>\n#include <cstdlib>\n#include <string>')
patched=host/'QuarterWidget.cpp';patched.write_text(s)
patch=''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='a/src/Gui/Quarter/QuarterWidget.cpp',tofile='b/src/Gui/Quarter/QuarterWidget.cpp'))
(repo/'examples/coinrender/freecad_sampling_policy.patch').write_text(patch)
flags={}
for line in (build/'src/Gui/CMakeFiles/FreeCADGui.dir/flags.make').read_text().splitlines():
 if ' = ' in line:k,v=line.split(' = ',1);flags[k]=shlex.split(v.replace(str(old),str(build)))
cmd=['/usr/bin/c++','-I'+str(repo/'experimental/include'),'-I'+str(root/'build-bgfx/include'),'-I'+str(repo/'include'),*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],'-o',str(host/'QuarterWidget.cpp.o'),'-c',str(patched)]
link=shlex.split((build/'src/Gui/CMakeFiles/FreeCADGui.dir/link.txt').read_text().replace(str(old),str(build)))
for i,arg in enumerate(link):
 if arg=='CMakeFiles/FreeCADGui.dir/Quarter/QuarterWidget.cpp.o':link[i]=str(host/'QuarterWidget.cpp.o')
 if 'coin-p16-bgfx-prefix/lib/libCoinRender.so' in arg:link[i]=str(root/'build-bgfx/lib/libCoinRender.so')
 if 'coin-p16-bgfx-prefix/lib/libCoin.so' in arg:link[i]=str(root/'build-bgfx/lib/libCoin.so')
 if arg=='-o':link[i+1]=str(build/'lib/libFreeCADGui.so')
# CMake's link list references relative object paths: execute from the copied Gui build.
commands=[cmd,link];(host/'commands.json').write_text(json.dumps(commands,indent=2))
for i,command in enumerate(commands):
 with (host/('compile.log' if i==0 else 'link.log')).open('w') as f:subprocess.run(command,cwd=build/'src/Gui',stdout=f,stderr=subprocess.STDOUT,check=True)
print('FreeCAD host rebuilt privately:',build)
