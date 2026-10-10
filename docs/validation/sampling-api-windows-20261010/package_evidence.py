import pathlib,shutil,json,hashlib,subprocess
root=pathlib.Path(__file__).resolve().parent
source=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
dest=source/'docs/validation/sampling-api-windows-20261010'
dest.mkdir(parents=True,exist_ok=True)
(dest/'.gitattributes').write_text('* -text\n',encoding='utf-8',newline='\n')
cohorts=['initial-bgfx','initial-wgpu','initial-wgpu-inputs','sdk-bgfx','sdk-bgfx-modules-c11',
         'sdk-wgpu-modules-c11','sdk-wgpu-crlf-fix','sdk-final','sampling','sampling-upload-recovery',
         'npot','npot-drain-recovery','npot-settle-recovery','window','policy-window','latency','suite','suite-recovery','ffi-recovery','camera','sdk-final-drain']
files=[p for p in root.iterdir() if p.is_file() and p.suffix in ['.py','.json','.log','.txt','.diff','.xml','.ppm']]
for name in cohorts:
    folder=root/name
    if folder.is_dir():files += [p for p in folder.rglob('*') if p.is_file() and p.suffix not in ['.dll','.exe','.pdb','.lib']]
for p in files:
    target=dest/p.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
(dest/'tested-source.diff').write_bytes(subprocess.check_output(['git','-c','core.autocrlf=false','-C',str(source),'diff','HEAD','--binary','--',
    'examples/coinrender/sdk-consumer','include/Inventor/system/gl-fallbacks.h','src','testsuite']))
shutil.copyfile(source/'examples/coinrender/sdk-consumer/capabilities.c',dest/'sdk-capabilities-source.c')
manifest=[]
for p in sorted(dest.rglob('*')):
    if p.is_file() and p.name!='manifest.json':manifest.append(dict(path=p.relative_to(dest).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(dest/'manifest.json').write_text(json.dumps(dict(base_revision='b86404cafc9ec13e91d0bac67f2e875f2b206a07',artifact_root=str(root),files=manifest),indent=2),encoding='utf-8',newline='\n')
print('files',len(manifest),'bytes',sum(p['bytes'] for p in manifest),'destination',dest)
