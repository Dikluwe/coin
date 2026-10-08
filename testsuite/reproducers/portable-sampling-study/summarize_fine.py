#!/usr/bin/env python3
import argparse, collections, csv, json, re, statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--artifacts',type=Path,required=True);a=p.parse_args();root=a.artifacts;result={'quality':{},'timings':{}}
for phase in ['quality-deep','quality-direct','quality-viewport','quality','quality-extra','quality-resources']:
 path=root/phase/'summary.json'
 if not path.exists():continue
 runs=json.loads(path.read_text());groups={}
 for mode in sorted({r['mode'] for r in runs}):
  own=[r for r in runs if r['mode']==mode];metrics=[]; nearest=[]
  for r in own:
   content=(root/phase/r['log']).read_text(errors='replace')
   metrics += [tuple(map(float,x)) for x in re.findall(r'samples=(\d+) mae=([0-9.]+) maximum=(\d+)',content)]
   nearest += [tuple(map(float,x)) for x in re.findall(r'quality-0\.500000[^\n]*CPU-GPU samples=(\d+) mae=([0-9.]+) max=(\d+)',content)]
  groups[mode]={'processes':len(own),'pass':sum(r['exit']==0 for r in own),'fail':sum(r['exit'] not in [0,77] for r in own),'skip':sum(r['exit']==77 for r in own),'failures':[{'profile':r['profile'],'case':r['case'],'exit':r['exit']} for r in own if r['exit'] not in [0,77]]}
  if metrics:groups[mode].update(comparisons=len(metrics),component_samples=sum(int(x[0]) for x in metrics),mae_max=max(x[1] for x in metrics),error_max=max(x[2] for x in metrics),divergences=sum(x[1]>1.5 or x[2]>4 for x in metrics))
  if nearest:groups[mode]['projective_nearest_cpu_gpu']={'comparisons':len(nearest),'mae_max':max(x[1] for x in nearest),'error_max':max(x[2] for x in nearest)}
 result['quality'][phase]=groups
for kind in ['gpu','window','city']:
 path=root/('benchmark-fine-'+kind)/'summary.json'
 if not path.exists():continue
 runs=json.loads(path.read_text());groups=collections.defaultdict(list)
 for r in runs:
  if r['exit'] or not r.get('timing_admitted',True):continue
  if kind=='gpu':values=r['gpu_ms']
  else:
   file=root/('benchmark-fine-'+kind)/(Path(r['log']).stem+'.csv')
   with file.open() as f:
    rows=[row for row in csv.DictReader(f) if row['warmup']=='0']
   key=next((k for k in rows[0] if k in ['render_present_ms','frame_ms','elapsed_ms','render_ms','milliseconds']),None)
   if not key:raise ValueError(list(rows[0]))
   values=[float(x[key]) for x in rows]
  groups[(r['gpu'],r['backend'],r['workload'],r['mode'])].append(values)
 result['timings'][kind]={'processes':len(runs),'failures':[r['log'] for r in runs if r['exit']], 'groups':[]}
 for (gpu,backend,workload,mode),values in sorted(groups.items()):
  process_medians=[statistics.median(v) for v in values];allvalues=sum(values,[])
  result['timings'][kind]['groups'].append(dict(gpu=gpu,backend=backend,workload=workload,mode=mode,processes=len(values),samples=len(allvalues),median_ms=statistics.median(allvalues),process_medians_ms=process_medians,min_ms=min(allvalues),max_ms=max(allvalues)))
(root/'results-fine.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
