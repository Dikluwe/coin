"""Package completed qualification records; exclude binaries and large image sets."""
import hashlib, json, pathlib, shutil, subprocess, xml.etree.ElementTree as ET
ROOT = pathlib.Path(__file__).resolve().parent
SOURCE = pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\coin-render-windows-20261007\coin')
DEST = SOURCE / 'docs/validation/windows-continuation-20261007'
DEST.mkdir(parents=True, exist_ok=True)
shutil.copy2(ROOT.parent/'Invoke-CMakeCleanEnv.ps1',DEST/'Invoke-CMakeCleanEnv.ps1')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
summary = []
for path in sorted(ROOT.glob('*.xml')):
    cases = ET.parse(path).findall('.//testcase')
    summary.append(dict(file=path.name, tests=len(cases),
        failures=[c.attrib['name'] for c in cases if c.find('failure') is not None],
        skipped=[c.attrib['name'] for c in cases if c.find('skipped') is not None],
        seconds=round(sum(float(c.attrib.get('time', 0)) for c in cases), 3)))
(ROOT/'test-summary.json').write_text(json.dumps(summary, indent=2))
manifest = {'tested_source_sha':(ROOT/'source-sha.txt').read_text().strip(), 'builds':{},'dependencies':{}}
dependency_source=pathlib.Path(r'H:\Git\coin\build\bgfx-windows-source')
for name, folder in [('bgfx.cmake',dependency_source)]+[(name,dependency_source/name) for name in ['bgfx','bx','bimg']]:
    manifest['dependencies'][name]=subprocess.check_output(['git','-C',str(folder),'rev-parse','HEAD'],text=True).strip()
prefix=pathlib.Path(r'H:\Git\coin\build\bgfx-windows-install')
manifest['dependencies']['installed_files']={str(p.relative_to(prefix)):sha(p)
    for p in [prefix/'bin/shaderc.exe',*sorted((prefix/'lib').glob('*.lib'))] if p.exists()}
manifest['shader_sources']={str(p.relative_to(SOURCE)):sha(p)
    for base in [SOURCE/'src/rendering/coinbgfx/shaders',SOURCE/'src/rendering/coinwgpu/rust_bridge']
    for p in sorted(base.rglob('*')) if p.is_file() and (p.suffix in ['.wgsl','.sc'] or p.name=='Cargo.lock')}
for backend in ['bgfx', 'wgpu']:
    build = ROOT/backend
    manifest['builds'][backend] = {str(p.relative_to(build)):sha(p)
        for folder, pattern in [('bin','*.dll'),('bin','*.exe')]
        for p in sorted((build/folder).glob(pattern))}
    for p in sorted(build.rglob('*.bin')):
        if 'shader' in str(p).lower():
            manifest['builds'][backend][str(p.relative_to(build))] = sha(p)
    for p in sorted((build/'src/rendering/coinrender/bgfx-generated').glob('*.h')):
        manifest['builds'][backend][str(p.relative_to(build))] = sha(p)
    cache = build/'CMakeCache.txt'
    (ROOT/(backend+'-cache-options.txt')).write_text('\n'.join(
        line for line in cache.read_text(errors='replace').splitlines()
        if line.startswith(('CMAKE_CXX_FLAGS:', 'CMAKE_GENERATOR:', 'CMAKE_GENERATOR_PLATFORM:',
                            'COIN_', 'BUILD_SHARED_LIBS:'))) + '\n')
(ROOT/'binary-manifest.json').write_text(json.dumps(manifest, indent=2))
for path in sorted(ROOT.iterdir()):
    if not path.is_file(): continue
    if path.name=='packaging-status.txt': continue
    if path.suffix not in ['.xml','.json','.txt','.py','.log','.csv']: continue
    # JUnit truncates successful stdout; LastTest preserves full diagnostics.
    shutil.copy2(path, DEST/path.name)
for subdir in ['sdk-consumer','performance']:
    origin = ROOT/subdir
    if not origin.exists(): continue
    for path in sorted(origin.rglob('*')):
        if not path.is_file() or path.suffix not in ['.cpp','.txt','.iv','.json','.csv','.log','.md']: continue
        target = DEST/subdir/path.relative_to(origin)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path,target)
for variant in ['bgfx-d3d12','wgpu-gl']:
    for choice in ['literal','reserve']:
        for path in (ROOT/'performance/verify').glob(f'geometry-100-{variant}-{choice}-1-*000006.ppm'):
            target=DEST/'performance/representative-images'/path.name
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,target)
files = {str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*'))
         if p.is_file() and p.name != 'evidence-sha256.json'}
(DEST/'evidence-sha256.json').write_text(json.dumps(files,indent=2))
print(json.dumps({'destination':str(DEST),'files':len(files),'xml_summary':summary},indent=2))
