import csv, json, re, statistics, hashlib
from pathlib import Path
import numpy as np
from PIL import Image
root=Path('H:/Git/coin'); out=root/'build/animated-buildings-windows-20261005'
runs=json.loads((out/'runs.json').read_text())
good=[r for r in runs if r['exit_code']==0]
assert len(good)==60, len(good)
groups={}; analyzed=[]; captures={}
for run in good:
    label=run['label']; mode=run['mode']
    log=(out/(label+'.log')).read_text(errors='replace')
    rows=list(csv.DictReader((out/(label+'.csv')).open()))
    measured=[r for r in rows if r['warmup']=='0']
    assert [int(r['frame_index']) for r in measured]==list(range(len(measured)))
    assert all(int(r['logical_frame'])==int(r['frame_index'])*3 for r in rows)
    warmups=int(run['command'][run['command'].index('--warmup')+1])
    assert sum(r['warmup']=='1' for r in rows)==warmups
    fields=['update_ms','render_ms','publication_ms','total_ms']
    stats={f:dict(median=statistics.median(float(r[f]) for r in measured),p95=sorted(float(r[f]) for r in measured)[int(np.ceil(len(measured)*.95))-1],max=max(float(r[f]) for r in measured)) for f in fields}
    record=dict(label=label,backend=run['backend'],api=run['api'],revision=run['revision'],round=run['round'],mode=mode,trace=run['trace'],samples=len(measured),warmups=warmups,stats=stats)
    analyzed.append(record)
    if run['trace']: continue
    key=(run['backend'],run['api'],run['revision'],mode,warmups)
    groups.setdefault(key,[]).append(record)
    entries=[dict(re.findall(r'(\w+)=(\S+)',line)) for line in log.splitlines() if line.startswith('capture backend=')]
    assert len(entries)==3
    captures[label]=entries
aggregate=[]
for key,items in groups.items():
    fields={f:dict(median_of_medians=statistics.median(i['stats'][f]['median'] for i in items),min_median=min(i['stats'][f]['median'] for i in items),max_median=max(i['stats'][f]['median'] for i in items)) for f in ['total_ms','update_ms','render_ms']}
    aggregate.append(dict(backend=key[0],api=key[1],revision=key[2],mode=key[3],warmups=key[4],processes=len(items),stats=fields))
pairs=[]
def compare(a,b,kind):
    assert len(captures[a])==len(captures[b])
    for x,y in zip(captures[a],captures[b]):
        assert x['frame_index']==y['frame_index'] and x['logical_frame']==y['logical_frame'] and x['state_fnv64']==y['state_fnv64']
        px=np.array(Image.open(x['image']).convert('RGB'),dtype=np.int16)
        py=np.array(Image.open(y['image']).convert('RGB'),dtype=np.int16)
        assert px.shape==py.shape==(768,768,3)
        delta=np.abs(px-py)
        pairs.append(dict(a=a,b=b,kind=kind,frame=int(x['frame_index']),mae=float(delta.mean()),max_channel_error=int(delta.max()),pixels_over_3=int(np.any(delta>3,axis=2).sum()),exact=bool(not delta.any()),rgb_a_sha256=hashlib.sha256(px.astype(np.uint8).tobytes()).hexdigest(),rgb_b_sha256=hashlib.sha256(py.astype(np.uint8).tobytes()).hexdigest()))
for run in good:
    if run['revision']!='after' or run['trace']: continue
    compare(run['label'],run['label'].replace('-after-','-before-'),'before_after')
    if run['mode']=='geometry':
        compare(run['label'],f"coingl-opengl-reference-geometry-{run['round']}",'vs_coingl')
equal=[p for p in pairs if p['kind']=='before_after']
gl=[p for p in pairs if p['kind']=='vs_coingl']
assert len(equal)==84 and all(p['exact'] for p in equal), [(p['a'],p['frame'],p['mae']) for p in equal if not p['exact']]
assert len(gl)==30
result=dict(scene_sha256=hashlib.sha256((root/'build/large-scenes/city-40000.iv').read_bytes()).hexdigest(),successful_processes=len(good),measured_frames=sum(r['samples'] for r in analyzed),warmup_frames=sum(r['warmups'] for r in analyzed),attempts_rejected_by_cli=[r for r in runs if r['exit_code']],runs=analyzed,aggregates=aggregate,image_pairs=pairs,before_after_exact_pairs=len(equal),coingl_pairs=len(gl),coingl_max_mae=max(p['mae'] for p in gl),coingl_max_channel_error=max(p['max_channel_error'] for p in gl),coingl_max_pixels_over_3=max(p['pixels_over_3'] for p in gl))
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print('Successful processes',len(good),'measured frames',result['measured_frames'],'warmups',result['warmup_frames'])
print('Before/after exact RGB pairs',len(equal),'CoinGL pairs',len(gl),'max MAE',result['coingl_max_mae'],'max pixels >3',result['coingl_max_pixels_over_3'])
for item in aggregate:
    if item['mode']=='geometry': print(item['backend'],item['api'],item['revision'],item['processes'],item['stats']['total_ms'])
