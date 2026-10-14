#!/usr/bin/env python3
import argparse,csv,gzip,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--artifacts',type=Path,required=True);root=p.parse_args().artifacts
def csv_open(path):
 return path.open() if path.exists() else gzip.open(str(path)+'.gz','rt')
result={'quality':{},'window':[],'million':{},'diagnostic':[]}
for phase in ['quality-wgpu-pilot','quality-deep','quality-direct','quality-viewport','quality','quality-resources','quality-shadow']:
 runs=json.loads((root/phase/'summary.json').read_text());result['quality'][phase]={}
 for mode in sorted({r['mode'] for r in runs}):
  own=[x for x in runs if x['mode']==mode];metrics=[]
  for x in own:metrics += [tuple(map(float,m)) for m in re.findall(r'samples=(\d+) mae=([0-9.]+) maximum=(\d+)',(root/phase/x['log']).read_text())]
  row=dict(processes=len(own),passed=sum(x['exit']==0 for x in own),failed=sum(x['exit'] not in [0,77] for x in own),skipped=sum(x['exit']==77 for x in own))
  if metrics:row.update(comparisons=len(metrics),error_max=max(m[2] for m in metrics),mae_max=max(m[1] for m in metrics))
  result['quality'][phase][mode]=row
runs=json.loads((root/'benchmark-fine-window/summary.json').read_text())
for key in sorted({(x['gpu'],x['backend'],x['workload'],x['mode']) for x in runs}):
 own=[x for x in runs if (x['gpu'],x['backend'],x['workload'],x['mode'])==key];allv=[];medians=[]
 for x in own:
  assert x['exit']==0 and x['timing_admitted']
  with csv_open(root/'benchmark-fine-window'/(Path(x['log']).stem+'.csv')) as f:vals=[float(z['render_present_ms']) for z in csv.DictReader(f) if z['warmup']=='0']
  allv+=vals;medians.append(statistics.median(vals))
 result['window'].append(dict(gpu=key[0],backend=key[1],workload=key[2],mode=key[3],median_ms=statistics.median(allv),process_medians_ms=medians,samples=len(allv)))
for phase in ['before','after']:
 runs=json.loads((root/('million-'+phase)/'summary.json').read_text());rows={}
 for mode in ['baseline','native','fine_uniform']:
  own=[x for x in runs if x['mode']==mode];assert len(own)==6;assert all(x['exit']==0 and x['monitor_on'] for x in own)
  v=[x['median_ms'] for x in own];rows[mode]=dict(processes=len(v),median_of_process_medians_ms=statistics.median(v),min_process_median_ms=min(v),max_process_median_ms=max(v),process_medians_ms=v)
 result['million'][phase]=rows
for x in json.loads((root/'million-diagnostic/summary.json').read_text()):
 assert x['exit']==0;s=(root/'million-diagnostic'/x['log']).read_text();v=[float(z) for z in re.findall(r'COIN_SAMPLING_AUDIT_GPU status=ok render_ms=([0-9.]+)',s)];assert len(v)==36
 records=re.findall(r'COIN_SAMPLING_AUDIT mode=\w+ revision=\d+ instanced=(\w+) instances=(\d+) draws=(\d+) texture_units=(\d+) uniform_bytes=(\d+) uniform_hash=([0-9a-f]+)',s);assert len(records)==36 and records[0][0]=='true' and all(z==records[0] for z in records)
 result['diagnostic'].append(dict(mode=x['mode'],repeat=x['repeat'],uniform_hash=records[0][5],uniform_bytes=int(records[0][4]),instance_count=int(records[0][1]),draw_count=int(records[0][2]),texture_units=int(records[0][3]),render_gpu_median_ms=statistics.median(v[12:]),samples=24))
assert len({x['uniform_hash'] for x in result['diagnostic']})==1
(root/'results-native.json').write_text(json.dumps(result,indent=2));print('saved',root/'results-native.json')
