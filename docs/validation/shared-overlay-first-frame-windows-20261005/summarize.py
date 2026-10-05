import hashlib
import json
from pathlib import Path
import re
import statistics
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image

ROOT=Path('H:/Git/coin')
OUT=ROOT/'build/shared-overlay-first-frame-windows-20261005'
SOURCE=ROOT/'build/coin-render-source'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def rgb(path):
    return np.asarray(Image.open(path).convert('RGB'),dtype=np.int16)

def difference(a,b):
    d=np.abs(a-b)
    return dict(mae_rgb=float(d.mean()),max_channel_error=int(d.max()),
        pixels_over_3=int(np.count_nonzero(d.max(axis=2)>3)),equal=bool(not d.any()))

runs=json.loads((OUT/'runs.json').read_text(encoding='utf-8'))
assert len(runs)==84,('Incomplete campaign',len(runs))
assert len({(row['kind'],row['variant'],row['revision'],str(row['sample'])) for row in runs})==84
for r in runs:
    assert r['exit_code']==0
    log=(OUT/r['log']).read_text(encoding='utf-8',errors='replace')
    r['first_ms']=float(re.search(r'_first_frame_ms=([\d.]+)',log)[1])
    r['warm_ms']=float(re.search(r'^\S+ frames=\d+ median_ms=([\d.]+)',log,re.M)[1])
    r['adapter']=re.search(r'^adapter=(.*)',log,re.M)[1]
    if r['variant']!='coingl':
        expected_api={'bgfx-opengl':'OpenGL','bgfx-vulkan':'Vulkan','bgfx-d3d12':'Direct3D 12',
                      'wgpu-opengl':'(Gl)','wgpu-vulkan':'(Vulkan)','wgpu-d3d12':'(Dx12)'}[r['variant']]
        assert expected_api in r['adapter'] and 'NVIDIA' in r['adapter'] and 'vendor_id=0x10de' in r['adapter'],r
    if r['kind']=='gl-diagnostic':
        assert 'GTX 1060' in log, 'CoinGL GPU identity missing'
    first=re.search(r'first_detail .*?before_frame_since_main_ms=([\d.]+) result_since_main_ms=([\d.]+)',log)
    if first: r['before_first_since_main_ms'],r['first_result_since_main_ms']=map(float,first.groups())
comparisons=[]
for variant in sorted({r['variant'] for r in runs if r['kind']=='static'}):
    sides={}
    for revision in ('before','after'):
        selected=[r for r in runs if r['variant']==variant and r['revision']==revision and r['kind']=='static']
        assert len(selected)==3
        sides[revision]=dict(first_median_ms=statistics.median(r['first_ms'] for r in selected),
            first_range_ms=[min(r['first_ms'] for r in selected),max(r['first_ms'] for r in selected)],
            warm_median_ms=statistics.median(r['warm_ms'] for r in selected),
            warm_range_ms=[min(r['warm_ms'] for r in selected),max(r['warm_ms'] for r in selected)])
    comparisons.append(dict(variant=variant,**sides,
        first_reduction_percent=100*(1-sides['after']['first_median_ms']/sides['before']['first_median_ms'])))
image_checks=[]
for before in (r for r in runs if r['kind']=='static' and r['revision']=='before'):
    after=next(r for r in runs if r['kind']=='static' and r['revision']=='after' and r['variant']==before['variant'] and r['sample']==before['sample'])
    image_checks.append(dict(kind='static-before-after',variant=before['variant'],sample=before['sample'],
        **difference(rgb(OUT/before['image']),rgb(OUT/after['image']))))
reference=next(r for r in runs if r['kind']=='static' and r['variant']=='coingl' and r['revision']=='after' and r['sample']==1)
reference_rgb=rgb(OUT/reference['image'])
for after in (r for r in runs if r['kind']=='static' and r['revision']=='after' and r['sample']==1 and r['variant']!='coingl'):
    image_checks.append(dict(kind='static-vs-coingl',variant=after['variant'],**difference(reference_rgb,rgb(OUT/after['image']))))
animation_checks=[]
motion=[]
for kind in ('animation','palette'):
    for case in sorted({r['sample'] for r in runs if r['kind']==kind}):
        selected=[r for r in runs if r['kind']==kind and r['sample']==case]
        frames={}
        for row in selected:
            text=(OUT/row['log']).read_text(encoding='utf-8',errors='replace')
            entries={}
            for line in text.splitlines():
                if line.startswith('capture '):
                    fields=dict(re.findall(r'(\w+)=(\S+)',line))
                    entries[int(fields['logical_frame'])]=fields
            assert sorted(entries)==[0,120,240,360,480],(row,entries)
            frames[row['variant']]=entries
            hashes=[digest(Path(f['image'])) for f in entries.values()]
            assert len(set(hashes))>1,(row,'No visible motion')
            motion.append(dict(kind=kind,case=case,variant=row['variant'],unique_images=len(set(hashes))))
        ref=frames['coingl']
        for variant,entries in frames.items():
            if variant=='coingl': continue
            for logical,fields in entries.items():
                assert fields['state_fnv64']==ref[logical]['state_fnv64'],(kind,case,variant,logical,'state mismatch')
                animation_checks.append(dict(kind=kind,case=case,variant=variant,logical_frame=logical,
                    state_fnv64=fields['state_fnv64'],**difference(rgb(Path(ref[logical]['image'])),rgb(Path(fields['image'])))))

assert len(animation_checks)==150 and len(motion)==35
assert all(check['equal'] for check in image_checks if check['kind']=='static-before-after')
tests={}
required=set()
for backend,apis in [('bgfx',('opengl','vulkan','d3d12')),('wgpu',('gl','vulkan','dx12'))]:
    for api in apis:
        name=f'tests-{backend}-{api}'
        required.add(name)
        cases=ET.parse(OUT/(name+'.xml')).findall('.//testcase')
        assert len(cases)==(16 if backend=='bgfx' else 17),(name,len(cases))
        assert all(case.find('failure') is None and case.find('skipped') is None for case in cases),name
        assert any(case.attrib['name']=='CoinRenderCameraReuseReferenceTest' for case in cases),name
        tests[name]=dict(total=len(cases),failures=0,skipped=0)
for backend in ('bgfx','wgpu'):
    name='tests-action-'+backend
    cases=ET.parse(OUT/(name+'.xml')).findall('.//testcase')
    assert len(cases)==1 and cases[0].find('failure') is None and cases[0].find('skipped') is None,name
    tests[name]=dict(total=1,failures=0,skipped=0)
probes={}
for backend in ('bgfx','wgpu'):
    probes[backend]={}
    for revision in ('before','after'):
        text=(OUT/f'probe-{backend}-{revision}.log').read_text()
        probes[backend][revision]=dict(
            payload_ms=float(re.search(r'object_qualification .*?payload_ms=([\d.]+)',text)[1]),
            first_ms=float(re.search(r'_first_frame_ms=([\d.]+)',text)[1]))
summary=dict(date='2026-10-05',base_commit='5d518cc300',branch='codex/coin-render-transform-performance',
    implementation='Read material/cube overlay options once per object payload qualification, refreshing on every invocation; no process-wide option cache or relaxed proof.',
    measurement='Windows 10 GTX 1060 6GB driver 581.08, serial fresh processes, static N3 interleaved, 1024x1024, 30 warmup + 120 measured, sync color readback + RGBA copy; retained system/driver caches.',
    animation_scope='Image smoke only, 2 warmup + 5 measured, logical frames 0/120/240/360/480, 10% updates. No animation performance claims.',
    comparisons=comparisons,image_checks=image_checks,animation_checks=animation_checks,motion=motion,
    tests=tests,probes=probes,runs=runs,
    baseline_hashes=json.loads((OUT/'baseline-hashes.json').read_text(encoding='utf-8-sig')),
    qualified_binary_hashes={backend:{name:digest(ROOT/f'build/coin-render-{backend}-msvc/bin'/name) for name in ('Coin4.dll','CoinRender4.dll','coin_render_gl_benchmark.exe')} for backend in ('bgfx','wgpu')})
if (OUT/'installed-hashes.json').exists():
    summary['installed']=json.loads((OUT/'installed-hashes.json').read_text(encoding='utf-8-sig'))
    for backend in ('bgfx','wgpu'):
        assert summary['installed'][backend]['binary_sha256']==summary['qualified_binary_hashes'][backend]
(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
for row in comparisons:
    print(row['variant'],round(row['before']['first_median_ms'],2),'->',round(row['after']['first_median_ms'],2),
          'gain%',round(row['first_reduction_percent'],2),'warm',round(row['after']['warm_median_ms'],2))
print('Tests passed:',sum(row['total'] for row in tests.values()),'image checks:',len(image_checks)+len(animation_checks))
print('Probes:',probes)
print('Max animation MAE:',max(row['mae_rgb'] for row in animation_checks))
