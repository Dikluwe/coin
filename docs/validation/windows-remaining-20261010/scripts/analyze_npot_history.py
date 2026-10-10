import pathlib,re,json,sys,subprocess,shutil,statistics,math
r=pathlib.Path(__file__).resolve().parent;cohort=sys.argv[1];out=r/cohort
subprocess.run([sys.executable,str(r/'analyze_npot.py'),'--cohort',cohort],check=False);shutil.copyfile(out/'gpu-summary.json',out/'latest-stats-summary.json');rows=json.loads((out/'gpu-summary.json').read_text())
for row in rows:
 if '-d3d12-' not in row['label']:continue
 source=next(x for x in json.loads((out/'summary.json').read_text()) if x['label']==row['label']);content=(out/source['log']).read_text(errors='replace');query_content=(out/(row['label']+'-queries.log')).read_text(errors='replace');queries={}
 for m in re.finditer(r'COIN_BGFX_D3D12_QUERY kind=(frame|view) view=(\d+) frame=(\d+) begin=(\d+) end=(\d+) frequency=(\d+)',query_content):
  kind,view,frame,begin,end,freq=m.groups();key=(kind,int(frame),int(view) if kind=='view' else -1);queries.setdefault(key,[]).append({'begin':int(begin),'end':int(end),'frequency':int(freq)})
 groups=[];pending=[];aborted=[]
 for m in re.finditer(r'COIN_RENDER_PHASE rtt_npot_mip_frame level=(\d+) frame=(\d+) reduce_view=(\d+)|COIN_RENDER_PHASE rtt_npot_mips_gpu levels=(\d+)',content):
  if m.group(4):groups.append(pending);pending=[]
  else:
   level,frame,view=map(int,m.groups()[:3])
   if level==1 and pending:aborted.append(pending);pending=[]
   pending.append(dict(level=level,frame=frame,view=view))
 samples=[]
 for group in groups:
  sample={'levels':len(group),'area_resolved':0,'frame_resolved':0,'area_ms':0.,'frame_ms':0.,'per_level':{},'query_records':[]}
  for v in group:
   resolved={}
   for kind,key,aggregate in [('view',('view',v['frame'],v['view']),'area_ms'),('frame',('frame',v['frame'],-1),'frame_ms')]:
    records=queries.get(key,[])
    if len(records)==1 and records[0]['frequency']>0 and records[0]['end']>records[0]['begin']:
     q=records[0];ms=(q['end']-q['begin'])*1000./q['frequency'];sample[aggregate]+=ms;sample['area_resolved' if kind=='view' else 'frame_resolved']+=1;resolved[kind]=dict(q,milliseconds=ms)
     if kind=='view':sample['per_level'][str(v['level'])]=ms
   sample['query_records'].append(dict(v,resolved=resolved))
  samples.append(sample)
 row['latest_stats_cost_gate']=row['gpu_cost_gate'];row['latest_stats_samples']=row['samples'];row['samples']=samples;row['aborted_mip_chains']=aborted;row['cost_source']='D3D12 SDK completed query history, exact (frame,view) join';row['total_samples']=len(samples);selected=samples[-30:];row['gpu_cost_gate']=row['functional_gate'] and len(samples)==44 and all(v['levels']==5 and v['area_resolved']==5 and v['frame_resolved']==5 and len(v['per_level'])==5 for v in selected)
 if row['gpu_cost_gate']:
  for key in ['area_ms','frame_ms']:
   a=[v[key] for v in selected];row[key+'_median']=statistics.median(a);row[key+'_p95']=sorted(a)[math.ceil(.95*len(a))-1]
 print(row['label'],'history samples',len(samples),'cost gate',row['gpu_cost_gate'],'aborted',len(aborted))
(out/'gpu-summary.json').write_text(json.dumps(rows,indent=2));raise SystemExit(0 if all(x['gpu_cost_gate'] for x in rows) else 1)
