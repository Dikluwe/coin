import json,hashlib,shutil,subprocess
from pathlib import Path
r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux')
repo=Path('/home/dikluwe/.codex/worktrees/coin-portable-sampling/coin')
out=repo/'docs/validation/sampling-api-linux-20261008';out.mkdir(parents=True,exist_ok=True)
def copy(p,rel=None):
 dest=out/(rel or p.relative_to(r));dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
# Whitelist evidence extensions, exclude preferences and authentication files.
for p in r.iterdir():
 if p.is_file() and p.suffix in ('.log','.json','.py'):copy(p)
for directory in r.iterdir():
 if not directory.is_dir() or directory.is_symlink() or directory.name in ('freecad-host','weston','baseline','scenes'):continue
 for p in directory.rglob('*'):
  if p.suffix=='.png' and directory.name not in ['freecad-warm-qualified-final','freecad-dpr2-isolated-warm-final','freecad-dpr1-desktop-warm-recovery','freecad-dpr2-desktop-warm-final','stock-portable-final']:continue
  if p.is_file() and p.suffix in ('.log','.json','.csv','.png','.FCMacro') and not any(s in p.parts for s in ['private-profile','profile','runtime']):copy(p)
for f in ['QuarterWidget.cpp','commands.json','compile.log','link.log']:
 copy(r/'freecad-host'/f)
for base in ['freecad-host/build','build-wgpu','build-bgfx']:
 b=r/base
 for f in ['CMakeCache.txt','compile_commands.json']:
  if (b/f).is_file():copy(b/f,Path('build-recipes')/base/f)
copy(r/'build-host.py')
trial_images=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size) for folder in r.glob('freecad-*') if folder.is_dir() and folder.name not in ['freecad-host','freecad-warm-qualified-final','freecad-dpr2-isolated-warm-final','freecad-dpr1-desktop-warm-recovery','freecad-dpr2-desktop-warm-final'] for p in folder.rglob('*.png')]
(out/'trial-image-manifest.json').write_text(json.dumps(dict(note='Trial images remain only in the durable local artifact root; they may contain desktop occlusion. Their hashes are retained; unrelated desktop contents are not copied to Git.',files=trial_images),indent=2))
source_files=['src/rendering/coinwgpu/rust_bridge/src/lib.rs','src/rendering/coinbgfx/CoinBgfxBackend.cpp','src/rendering/coinbgfx/CoinBgfxLowering.cpp','src/rendering/coinbgfx/CoinBgfxLowering.h','src/rendering/CoinRenderSamplingCore.h','testsuite/qt-quarter/run.py','testsuite/qt-quarter/test_runner.py','testsuite/qt-quarter/freecad_sampling_policy.FCMacro','testsuite/qt-quarter/freecad_screen_content.FCMacro','testsuite/qt-quarter/freecad_overlays.FCMacro','examples/coinrender/freecad_sampling_policy.patch','testsuite/reproducers/sampling-api/run.py','testsuite/reproducers/sampling-api/benchmark_linux.py','testsuite/reproducers/sampling-api/qualify_freecad_linux.py']
# Sampling core is located with tracked path lookup, preserving the exact source.
source_files=[s for s in source_files if (repo/s).exists()]
source_files+=subprocess.check_output(['git','ls-files','*CoinRenderSamplingCore.h'],cwd=repo,text=True).splitlines()
for f in source_files:copy(repo/f,Path('source')/f)
for f in (r.parent/'20261008-sampling-api').glob('*c*probe*.c'):copy(f,Path('source')/f.name)
hash_file=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[]
for backend in ['wgpu','bgfx']:
 for area in ['bin','lib']:
  for p in (r/('build-'+backend)/area).iterdir():
   if p.is_file() and not p.is_symlink():files.append(dict(path=str(p),sha256=hash_file(p),bytes=p.stat().st_size))
for p in [r/'freecad-host/build/bin/FreeCAD',r/'freecad-host/build/lib/libFreeCADGui.so']:
 files.append(dict(path=str(p),sha256=hash_file(p),bytes=p.stat().st_size))
source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()
(out/'runtime-final.json').write_text(json.dumps(dict(source_commit=source_commit,files=files,limitation='Final runtime frozen after adapter-name and GL receipt/window-geometry diagnostic rebuilds. Original performance cohorts used f9e606d4c3021bad9aaeebc67b9be632c9b25ac8. Its shared-library ELF hashes were not frozen before rebuilding the diagnostic; these final hashes do not certify historical f9 runtime.'),indent=2))
shaderpaths=subprocess.check_output(['git','ls-files','src/rendering/coinwgpu/shaders','src/rendering/coinbgfx/shaders'],cwd=repo,text=True).splitlines()
shaderdata=[]
for f in shaderpaths:
 before=subprocess.check_output(['git','rev-parse','f9e606d4c:'+f],cwd=repo,text=True).strip()
 after=subprocess.check_output(['git','rev-parse','HEAD:'+f],cwd=repo,text=True).strip()
 assert before==after
 shaderdata.append(dict(path=f,git_blob=after,sha256=hash_file(repo/f)))
(out/'shader-identity.json').write_text(json.dumps(shaderdata,indent=2))
(out/'diagnostic-only-runtime.diff').write_bytes(subprocess.check_output(['git','diff','f9e606d4c','HEAD','--','src/rendering/coinwgpu/rust_bridge/src/lib.rs','src/rendering/coinbgfx/CoinBgfxBackend.cpp'],cwd=repo))
old=json.loads((r/'baseline-manifest.json').read_text())
assert all(hash_file(Path(f['path']))==f['sha256'] for f in old['files'])
scenes=[dict(path=str(p),bytes=p.stat().st_size,sha256=hash_file(p)) for p in (r/'scenes').glob('*.iv')]
(out/'scene-manifest.json').write_text(json.dumps(scenes,indent=2))
# Manifest excludes itself and is verified immediately after writing.
manifest=[]
for p in sorted(out.rglob('*')):
 if p.is_file() and p.name!='manifest.json':manifest.append(dict(path=str(p.relative_to(out)),bytes=p.stat().st_size,sha256=hash_file(p)))
(out/'manifest.json').write_text(json.dumps(dict(source_commit=source_commit,files=manifest),indent=2))
assert all(hash_file(out/f['path'])==f['sha256'] for f in manifest)
print('Verified',len(manifest),'files,',sum(f['bytes'] for f in manifest),'bytes; baseline',len(old['files']),'hashes match')
