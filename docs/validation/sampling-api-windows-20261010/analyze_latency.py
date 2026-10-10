import pathlib,json,csv,statistics,math,hashlib
root=pathlib.Path(__file__).resolve().parent/'latency';rows=[]
for record in json.loads((root/'summary.json').read_text()):
    path=root/(record['label']+'.csv');samples=list(csv.DictReader(path.open())) if path.is_file() else []
    measured=[s for s in samples if s['warmup']=='0'];values=[float(s['render_present_ms']) for s in measured]
    log=(root/record['log']).read_text(encoding='utf-8',errors='replace')
    valid=record['exit']==0 and len(samples)==660 and len(measured)==600 and 'readback=none' in log and 'result=1' in log
    result=dict(label=record['label'],exit=record['exit'],pass_gate=valid,total_frames=len(samples),measured_frames=len(measured),metric='CPU render/present return; includes backpressure; no GPU completion/display latency claim',clock_log=record['label']+'-clocks.csv')
    if valid:result.update(median_ms=statistics.median(values),p95_ms=sorted(values)[math.ceil(.95*len(values))-1])
    rows.append(result);print(record['label'],valid,result.get('median_ms'),result.get('p95_ms'))
(root/'timing-summary.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['pass_gate'] for r in rows) else 1)
