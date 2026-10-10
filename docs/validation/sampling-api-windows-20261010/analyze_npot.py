import pathlib,re,json,statistics,math,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument('--cohort',default='npot');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent/a.cohort
rows=[]
pattern=re.compile(r'^COIN_RENDER_PHASE rtt_npot_mips_gpu levels=(\d+) area_resolved=(\d+) area_ms=([^ ]+) frame_resolved=(\d+) frame_ms=([^ ]+)(.*)$')
for row in json.loads((root/'summary.json').read_text()):
    lines=(root/row['log']).read_text(encoding='utf-8',errors='replace').splitlines()
    samples=[]
    for line in lines:
        m=pattern.match(line)
        if not m:continue
        n,an,area,fn,frame,tail=m.groups()
        samples.append(dict(levels=int(n),area_resolved=int(an),frame_resolved=int(fn),area_ms=None if area=='unavailable' else float(area),frame_ms=None if frame=='unavailable' else float(frame),per_level={k:float(v) for k,v in re.findall(r'level(\d+)_ms=([0-9.]+)',tail)}))
    api=row['label'].split('-')[1];expected={'d3d12':4,'vulkan':1,'opengl':2}[api]
    receipt=next((s for s in lines if s.startswith('NPOT combined adapter=')),'')
    finish=next((s for s in lines if s.startswith('NPOT shadow transparency')),'')
    selected=samples[-30:]
    functional=row['exit']==0 and 'qualified=1' in finish and 'NVIDIA' in receipt and 'renderer='+str(expected)+' ' in receipt
    timer=functional and len(samples)==44 and len(selected)==30 and all(s['levels']==5 and s['area_resolved']==5 and s['frame_resolved']==5 and len(s['per_level'])==5 and s['area_ms'] is not None and s['frame_ms'] is not None for s in selected)
    result=dict(label=row['label'],command=row['command'],exit=row['exit'],functional_gate=functional,gpu_cost_gate=timer,receipt=receipt,finish=finish,total_samples=len(samples),measured_samples=len(selected),samples=samples,log_sha256=row['log_sha256'])
    if timer:
        for key in ['area_ms','frame_ms']:
            values=[s[key] for s in selected];result[key+'_median']=statistics.median(values);result[key+'_p95']=sorted(values)[math.ceil(.95*len(values))-1]
    rows.append(result)
    print(row['label'],'functional',functional,'GPU',timer,'samples',len(samples),'median',result.get('frame_ms_median'))
(root/'gpu-summary.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['functional_gate'] and r['gpu_cost_gate'] for r in rows) else 1)
