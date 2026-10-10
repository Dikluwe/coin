import json,re,statistics
from pathlib import Path
r=Path('/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux')
load=lambda folder:json.loads((r/folder/'summary.json').read_text())
oldcpu=json.loads((r/'cpu-aggregate.json').read_text());before={(x['profile'],x['workload']):x.get('portable_before_ms') for x in oldcpu}
def combine(old,new):
 rows=[]
 for folder in [old,new]:
  for x in load(folder):
   if folder==old and x['profile']=='bgfx-amd-gl':continue
   assert x['valid'];rows.append(dict(x,campaign=folder))
 return rows
cpu=combine('benchmark-cpu-final','benchmark-gl-receipt-final');base=combine('benchmark-baseline','benchmark-gl-baseline-receipt-final');gpu=[dict(x,campaign='benchmark-gpu') for x in load('benchmark-gpu')]
assert len(cpu)==144 and len(base)==120 and len(gpu)==96
hardware=[]
for kind,rows in [('cpu',cpu),('baseline',base),('gpu',gpu)]:
 for x in rows:
  log=(r/x['campaign']/x['log']).read_text();rep=x['report'];profile=x['profile']
  if profile=='bgfx-amd-gl':
   proof=re.findall(r'^COIN_RENDER_PHASE bgfx_gl_adapter vendor=(.*?) renderer=(.*?) version=(.*)$',log,re.MULTILINE)
   ok=bool(proof) and all('amd radeon' in (v+' '+n).lower() and bool(ver) and not any(w in (v+' '+n).lower() for w in ['llvmpipe','lavapipe','softpipe','software','virgl','cpu']) for v,n,ver in proof)
  else:
   vendor='0x1002' if '-amd-' in profile else '0x10de';name='AMD' if '-amd-' in profile else 'NVIDIA'
   ok='vendor_id='+vendor+' ' in rep and name in rep
   proof=rep
  assert ok,(kind,x['campaign'],x['log'])
  hardware.append(dict(kind=kind,campaign=x['campaign'],log=x['log'],physical=True,proof=proof))
  if kind=='gpu':assert len(x['gpu_values_ms'])==45
assert sum(len(x['gpu_values_ms']) for x in gpu)==4320
(r/'hardware-audit-final.json').write_text(json.dumps(hardware,indent=2))
for name,rows,metric,policies in [('cpu',cpu,'cpu_median_ms',['native','portable']),('gpu',gpu,'gpu_median_ms',['native','portable']),('baseline',base,'cpu_median_ms',['baseline','native'])]:
 out=[]
 for profile,work in sorted({(x['profile'],x['workload']) for x in rows}):
  result=dict(profile=profile,workload=work)
  for policy in policies:
   values=[x[metric] for x in rows if x['profile']==profile and x['workload']==work and x['policy']==policy]
   assert len(values)==2
   result[policy+'_ms']=statistics.median(values);result[policy+'_range_ms']=[min(values),max(values)]
  result[policies[1]+'_over_'+policies[0]]=result[policies[1]+'_ms']/result[policies[0]+'_ms']
  if name=='cpu' and before.get((profile,work)) is not None:result['portable_before_ms']=before[profile,work]
  out.append(result)
 (r/(name+'-aggregate-final.json')).write_text(json.dumps(out,indent=2))
index=dict(cpu=[dict(campaign='benchmark-cpu-final',exclude_profiles=['bgfx-amd-gl']),dict(campaign='benchmark-gl-receipt-final')],baseline=[dict(campaign='benchmark-baseline',exclude_profiles=['bgfx-amd-gl']),dict(campaign='benchmark-gl-baseline-receipt-final')],gpu=[dict(campaign='benchmark-gpu')],notes='GL CPU/baseline original rows excluded because physical context receipts were absent; replaced by separate repeats with standalone GL hardware receipts (diagnostic-only backend change). Frozen GL A/B requires full phase instrumentation on both sides; other native controls and ordinary CPU campaigns stay uninstrumented. Historical evidence retained. Main f9 campaign libraries were not frozen before wgpu adapter-name diagnostic rebuild; do not certify historical ELF from final hashes.')
(r/'campaign-index.json').write_text(json.dumps(index,indent=2))
repeat=load('benchmark-nvidia-million-repeat');assert len(repeat)==4 and all(x['valid'] for x in repeat)
extra=[]
for policy in ['native','portable']:
 values=[x['cpu_median_ms'] for x in repeat if x['policy']==policy];extra.append(dict(policy=policy,median_ms=statistics.median(values),range_ms=[min(values),max(values)]))
(r/'nvidia-repeat-aggregate.json').write_text(json.dumps(extra,indent=2))
print('Physical proofs verified for',len(hardware),'measurements; GPU timestamps=4320; Nvidia repeat',extra)
