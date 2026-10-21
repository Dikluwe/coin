#!/usr/bin/env python3
"""Verify the original B04 Linux fixture retest without rewriting historical results."""
from pathlib import Path
import gzip,hashlib,json,re,sys
import numpy as np

def require(value,message):
 if not value:raise ValueError(message)
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
 root=Path(sys.argv[1]);repo=root.parents[2];old=json.loads((repo/'docs/inventories/coin-render-visual-case-ledger.json').read_text())
 expected={x['id']:x for x in old['processes'] if x['platform']=='linux'}
 failures=json.loads((root/'linux-failure-retest-results.json').read_text())
 controls=json.loads((root/'linux-pass-control-results.json').read_text())
 rows=failures+controls
 require(len(failures)==32 and len(controls)==16 and len(rows)==len(expected)==48 and {x['id'] for x in rows}==set(expected),'incomplete 48-case Linux retest')
 manifest=json.loads((root/'manifest.json').read_text())
 for name,digest in manifest['evidence_sha256'].items():require(sha((root/name).read_bytes())==digest,'changed evidence '+name)
 identity=json.loads((root/'build-identity.json').read_text())
 fixture=(root/'source/original-linux-visual-main.cpp').read_bytes()
 require(sha(fixture)==manifest['fixture_sha256'],'fixture source changed')
 summaries=[]
 for row in rows:
  original=expected[row['id']]
  require(row['original_result']==original['original_result'],f'{row["id"]} original result changed')
  require(row['executable_sha256']==identity[row['backend']+'_executable_sha256'],f'{row["id"]} unexpected executable')
  for key in ['name','backend','api','policy','scene','cohort']:
   require(row[key]==original[key],f'{row["id"]} wrong {key}')
  require(sha(Path(original['log']).read_bytes())==original['log_sha256'],f'{row["id"]} historical log changed')
  log=(root/'retest'/row['log']).read_text();require('receipt renderer=' in log and 'workload='+row['scene'] in log and 'policy='+row['policy'] in log,f'{row["id"]} missing physical receipt')
  receipt=re.search(r'^receipt renderer=(\d+) vendor=([0-9a-f]+) device=([0-9a-f]+) adapter=([^\n]+)',log,re.M)
  require(receipt is not None,f'{row["id"]} malformed receipt')
  vendor=int(receipt[2],16);device=int(receipt[3],16);renderer=int(receipt[1]);api=row['api'];backend=row['backend']
  if api=='vulkan':require(renderer==1 and vendor==0x10de and device==0x2560 and 'NVIDIA' in receipt[4],f'{row["id"]} wrong NVIDIA/Vulkan')
  elif backend=='wgpu':require(renderer==2 and vendor==0x1002 and 'AMD' in receipt[4],f'{row["id"]} wrong AMD/wgpu GL')
  else:
   adapter=re.search(r'bgfx_gl_adapter vendor=([^\n]+)',log)
   require(renderer==2 and adapter and 'renderer=AMD Radeon' in adapter[1],f'{row["id"]} wrong AMD/BGFX GL')
  original_receipts=re.findall(r'surface_oracle stage=(\w+) rgb_max=(\d+) exact=(\d+)',log)
  require([list(x) for x in original_receipts]==row['oracles'],f'{row["id"]} oracle text mismatch')
  shots={(x['stage'],x['kind']):x for x in row['snapshots']}
  for stage in ['initial','final']:
   if (stage,'window') not in shots or (stage,'offscreen') not in shots:continue
   arrays=[]
   for kind in ['window','offscreen']:
    item=shots[(stage,kind)];path=root/'retest'/item['file'];zipdata=path.read_bytes()
    require(sha(zipdata)==item['sha256_gzip'],f'{row["id"]} image archive changed')
    raw=gzip.decompress(zipdata);require(sha(raw)==item['sha256_raw'],f'{row["id"]} image payload changed')
    head=b'P6\n1280 960\n255\n';require(raw.startswith(head) and len(raw)==len(head)+1280*960*3,f'{row["id"]} image size wrong')
    arrays.append(np.frombuffer(raw,dtype=np.uint8,offset=len(head)))
   a,b=arrays;maximum=int(np.abs(a.astype(np.int16)-b.astype(np.int16)).max());coverage=int(np.count_nonzero(a.reshape((-1,3)).any(axis=1)))
   require(all(shots[(stage,k)]['rgb_max']==maximum and shots[(stage,k)]['coverage']==coverage for k in ['window','offscreen']),f'{row["id"]} pixel computation mismatch')
   matching=[x for x in original_receipts if x[0]==stage]
   require(len(matching)==1 and int(matching[0][1])==maximum,f'{row["id"]} original fixture receipt disagrees with image')
  passed=(row['returncode']==0 and len(original_receipts)==2 and all(x[2]=='1' for x in original_receipts) and row['physical_gpu_verified'] and len(shots)==4 and all(shots[(s,'window')]['rgb_max']==0 and shots[(s,'window')]['coverage']>1280*960//10 for s in ['initial','final']))
  require(row['current_result']==('PASS' if passed else 'FAIL'),f'{row["id"]} result misclassified')
  summaries.append({'id':row['id'],'original':row['original_result'],'current':row['current_result'],'reason':[(x[0],x[1],x[2]) for x in original_receipts],'returncode':row['returncode']})
 result={'linux_processes_retested':len(rows),'original_failures_retested':len(failures),'historical_pass_controls_retested':len(controls),'now_pass':sum(x['current']=='PASS' for x in summaries),'remain_fail':sum(x['current']=='FAIL' for x in summaries),'case_results':summaries,'historical_results_preserved':True,'pixel_archives_verified':True,'B04_linux_cells_closed':all(x['current']=='PASS' for x in summaries)}
 (root/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='case_results'},indent=2))
if __name__=='__main__':main()
