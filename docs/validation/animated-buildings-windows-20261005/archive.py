import hashlib, json, shutil
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path('H:/Git/coin'); out=root/'build/animated-buildings-windows-20261005'
installed=[]
for backend,prefix in [('bgfx','coin-render-bgfx-install'),('wgpu','coin-render-install')]:
    for name in ['Coin4.dll','CoinRender4.dll','coin_render_gl_benchmark.exe']:
        built=root/f'build/coin-render-{backend}-msvc/bin'/name
        target=root/f'build/{prefix}/bin'/name
        sha=hashlib.sha256(built.read_bytes()).hexdigest()
        assert sha==hashlib.sha256(target.read_bytes()).hexdigest()
        installed.append(dict(backend=backend,file=name,build=str(built),installed=str(target),sha256=sha))
(out/'installed-hashes.json').write_text(json.dumps(installed,indent=2)+'\n')
tests=[]
for path in sorted(out.glob('tests-*.xml')):
    node=ET.parse(path).getroot(); cases=node.findall('testcase')
    assert int(node.attrib.get('failures',0))==int(node.attrib.get('skipped',0))==0
    assert len(cases)==(17 if '-bgfx-' in path.name else 18)
    assert '[SKIP]' not in path.read_text(errors='replace')
    tests.append(dict(suite=path.stem,cases=len(cases),failures=0,skipped=0))
assert len(tests)==6 and sum(t['cases'] for t in tests)==105
(out/'test-summary.json').write_text(json.dumps(tests,indent=2)+'\n')
dest=root/'build/coin-render-source/docs/validation/animated-buildings-windows-20261005'
dest.mkdir(parents=True,exist_ok=True)
for pattern in ['*.py','*.ps1','*.json','*.csv','*.log','selected-*-tests.txt','tests-*.xml']:
    for path in out.glob(pattern): shutil.copy2(path,dest/path.name)
shutil.copy2(root/'build/city-motion-20261005/profile_geometry.py',dest/'profile_geometry.py')
diagnostic=dest/'coingl-gpu-diagnostic.log'
raw_diagnostic=out/'coingl-gpu-diagnostic.log'
(dest/'diagnostic-raw-sha256.json').write_text(json.dumps({str(raw_diagnostic):hashlib.sha256(raw_diagnostic.read_bytes()).hexdigest()},indent=2)+'\n')
# Preserve raw stdout in build; the versioned text has no trailing blanks.
diagnostic.write_bytes(b'\n'.join(line.rstrip(b' \t') for line in diagnostic.read_bytes().splitlines())+b'\n')
manifest={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(dest.iterdir()) if p.is_file() and p.name not in ['evidence-hashes.json','README.md']}
(dest/'evidence-hashes.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Installed hashes match; 105 tests passed; archived',len(manifest),'evidence files.')
