#!/usr/bin/env python3
"""Derive tables from measured runs; preserve all failures and timing scopes."""
import argparse,json,statistics as st,re
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('artifacts',type=Path);a=ap.parse_args();result={'quality':{},'gpu':{},'window':{}}
for n in ['quality','quality-optimized','quality-alpha']:
 r=json.loads((a.artifacts/n/'summary.json').read_text());result['quality'][n]={'runs':len(r),'pass':sum(x['exit']==0 for x in r),'fail':sum(x['exit']!=0 for x in r)}
for n in ['benchmark-gpu','benchmark-gpu-optimized']:
 r=json.loads((a.artifacts/n/'summary.json').read_text());d={}
 for g in ['amd','nvidia']:
  for u in [1,4,8]:
   for q in [0,1]:
    values={str(m):st.median([v for x in r if x['gpu']==g and x['workload']==f'{u}-{q}' and x['mode']==m for v in x['gpu_ms']]) for m in sorted(set(x['mode'] for x in r))}
    d[f'{g}-{u}-{q}']=values
 result['gpu'][n]=d
for n in ['benchmark-window','benchmark-window-optimized']:
 r=json.loads((a.artifacts/n/'summary.json').read_text());d={}
 for g in ['amd','nvidia']:
  for w in sorted(set(x['workload'] for x in r)):
   values={}
   for m in sorted(set(x['mode'] for x in r if x['workload']==w)):
    v=[float(re.search(r'cpu_frame_median_ms=([0-9.]+)',x['report']).group(1)) for x in r if x['gpu']==g and x['workload']==w and x['mode']==m and x['exit']==0]
    if v:values[m]={'median_of_process_medians_ms':st.median(v),'process_medians_ms':v}
   d[f'{g}-{w}']=values
 result['window'][n]=d
(a.artifacts/'results.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result['quality']))
