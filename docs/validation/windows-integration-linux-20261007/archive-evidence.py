from pathlib import Path
import json, shutil, hashlib, subprocess, xml.etree.ElementTree as ET
repo=Path('/tmp/coin-render-first-frame')
src=Path('/tmp/coin-windows-integration-linux-20261007')
dst=repo/'docs/validation/windows-integration-linux-20261007'
dst.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in src.iterdir():
    if p.is_file() and p.suffix in ('.json','.py','.log'):
        shutil.copy2(p,dst/p.name)
    elif p.is_dir() and p.name.startswith(('amd-','nvidia-','sdk-amd-','sdk-nvidia-')):
        shutil.copytree(p,dst/p.name,dirs_exist_ok=True)
shutil.copy2(Path(__file__),dst/'archive-evidence.py')
(dst/'.gitattributes').write_text('* -text\n')
code=['src/rendering/coinrender/CMakeLists.txt','examples/coinrender/CMakeLists.txt']
patch=subprocess.check_output(['git','diff','--',*code],cwd=repo)
(dst/'implementation.patch').write_bytes(patch)
source_hashes={}
for name in code+['examples/coinrender/sdk-consumer/main.cpp','examples/coinrender/sdk-consumer/CMakeLists.txt','examples/coinrender/sdk-consumer/README.md']:
    p=repo/name
    q=dst/'source-snapshot'/name
    q.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(p,q)
    source_hashes[name]=sha(p)
artifacts={}
for backend in ('bgfx','wgpu'):
    sdk=src/f'relocated-sdk-{backend}'
    for p in (sdk/'lib/cmake/CoinRender').glob('*.cmake'):
        q=dst/'sdk-export'/backend/p.name
        q.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(p,q)
    paths=list((sdk/'include').rglob('*.h'))
    paths+=list((sdk/'share/doc/Coin/examples/coinrender/sdk-consumer').glob('*'))
    paths+=[sdk/'lib/libCoinRender.so',sdk/'lib/libCoin.so.80.0.10',src/f'consumer-{backend}-relocated/coin-render-sdk-consumer']
    build=Path(f'/tmp/coin-render-first-frame-{backend}')
    paths+=list((build/'bin').glob('*Test*'))
    paths+=[build/'lib/libCoinRender.so',build/'lib/libCoin.so.80.0.10']
    artifacts[backend]={str(p):sha(p) for p in paths if p.is_file()}
suites=[]
for p in sorted(dst.glob('*/tests.xml')):
    root=ET.parse(p).getroot()
    assert int(root.get('failures','0'))==0 and int(root.get('skipped','0'))==0
    tests=root.findall('testcase')
    assert len(tests)==int(root.get('tests'))
    suites.append({'path':str(p.relative_to(dst)),'tests':len(tests),'failures':0,'skipped':0,'names':[t.get('name') for t in tests]})
assert sum(s['tests'] for s in suites)==163
for name in ('results.json','rtt-results.json'):
    assert all(r['exit']==0 for r in json.loads((dst/name).read_text()))
sdk_results=json.loads((dst/'sdk-results.json').read_text())
assert len(sdk_results)==9
for result in sdk_results:
    assert result['exit']==result.get('expected_exit',0)
    if result['exit']==0:
        output=(dst/f"sdk-{result['key']}"/'run.log').read_text()
        assert 'sdk_CoinGL_rgb_max=0 reference_ready=1' in output and 'pixels_ok=1' in output
        assert 'llvmpipe' not in output.lower()
manifest={
    'source_base':'8d9f47305875d5cde92935acc383a7dcbff6fecc',
    'previous_linux_base':'0d7ba61b7cc78318cb3ec7fdaa1efabe01cdeb23',
    'branch':'codex/coin-render','integration':'fast-forward; history retained',
    'source_snapshot_sha256':source_hashes,
    'artifact_sha256':artifacts,
    'artifact_stage':'Library/test binary hashes collected after final SDK full rebuild/relink; regression campaign ran on imported source before CMake-only SDK export/install edits. SDK seven cells and CoinBgfxCoreTest ran after those edits.',
    'wgpu_protocol_revision':49,
    'test_suites':suites,'selected_ctest_total':163,'cpu_tests_separate':10,
    'bgfx_build_interface_test_separate':1,'camera_processes_separate':6,
    'sdk_positive_processes':7,'sdk_rgb_max':0,'sdk_d3d12_negative_processes':2,'sdk_negative_expected_exit':3,
    'windows_artifacts_verified_checkout_and_index':796,
    'preparation_failures':'Initial BGFX find_dependency missing; initial wgpu install RPATH before full relink; source-directory cache mismatch. Retained logs; not counted as passes.',
    'hardware':{'amd':'1002:1638 Renoir / Mesa 25.2.8','nvidia':'10de:2560 RTX 3060 Laptop / 610.57.04','kernel':'6.17.0-42-generic','compiler':'GCC 13.3 Release'},
    'scope':'Serial selected regressions, CPU, cameras, installed relocated public SDK smoke. No full-suite or new Windows qualification; no performance conclusions.',
}
(dst/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(dst/'README.md').write_text('''# Evidência de integração Windows e continuação Linux\n\nRelatório: [integração Linux](../../coin-render-windows-integration-linux-20261007.md).\n`manifest.json` registra source/base, escopo, resultados e hashes dos artifacts locais.\n`artifact-sha256.json` cobre todos os arquivos deste pacote, exceto ele próprio.\nOs scripts preservam paths/comandos deste host; reproduzir em diretório novo.\nFalhas de preparação ficam preservadas e não são contadas como passes.\nSDKs e binários completos continuam locais; headers/binários são identificados\npor SHA-256. Os exports e fontes exatos testados estão arquivados.\nAtributo `* -text` preserva os bytes dos logs/XML/patches.\n''')
hashes={str(p.relative_to(dst)):sha(p) for p in sorted(dst.rglob('*')) if p.is_file() and p.name!='artifact-sha256.json'}
(dst/'artifact-sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
assert all(sha(dst/name)==expected for name,expected in hashes.items())
print(json.dumps({'archived_files':len(hashes)+1,'bytes':sum(p.stat().st_size for p in dst.rglob('*') if p.is_file()),'ctest':163,'sdk':7}))
