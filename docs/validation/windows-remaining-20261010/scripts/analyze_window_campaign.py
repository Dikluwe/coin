import pathlib,json,csv,statistics,math,sys
r=pathlib.Path(sys.argv[1]);out=[]
for row in json.loads((r/'summary.json').read_text()):
 records=list(csv.DictReader((r/(row['label']+'.csv')).open())); measured=[v for v in records if v['warmup']=='0']; result=dict(label=row['label'],pass_gate=row['pass_gate'],measured_frames=len(measured))
 for key in ['event_ms','update_ms','render_present_ms','total_ms']:
  v=[float(x[key]) for x in measured];result[key]={'median':statistics.median(v),'p95':sorted(v)[math.ceil(.95*len(v))-1],'max':max(v)}
 clocks=list(csv.DictReader((r/(row['label']+'-clocks.csv')).open()));result['clock_samples']=len(clocks);result['clock_columns']={k:sorted(set(x.get(k,'') for x in clocks)) for k in (clocks[0] if clocks else []) if 'clock' in k or 'pstate' in k or 'temperature' in k}
 out.append(result)
(r/'latency-summary.json').write_text(json.dumps(out,indent=2));print(len(out),'processes',sum(x['measured_frames'] for x in out),'measured frames',sum(x['pass_gate'] for x in out),'passes')
