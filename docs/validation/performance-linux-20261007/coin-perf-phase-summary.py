"""Warm CPU spans from frame-delimited traces; nested spans are not additive."""
import json,re,statistics
from pathlib import Path
root=Path('/tmp/coin-perf-profile-desktop');rows=[]
for path in sorted(root.glob('*.log')):
 frames=[];current={};postcommit=[]
 for line in path.read_text().splitlines():
  if not line.startswith('COIN_RENDER_PHASE '):continue
  tag=line.split()[1]
  fields={k:float(v) for k,v in re.findall(r'(\w+_ms)=([\d.eE+-]+)(?:\s|$)',line)}
  if tag=='capture_camera_basis':
   postcommit.append(fields);continue
  current.setdefault(tag,[]).append(fields)
  if tag=='action':frames.append(current);current={}
 assert len(frames)==6,(path,len(frames))
 warm=frames[2:6];aggregate={}
 for tag in sorted({tag for frame in warm for tag in frame}):
  aggregate[tag]={}
  for field in sorted({k for frame in warm for record in frame.get(tag,[]) for k in record}):
   values=[sum(r.get(field,0) for r in frame.get(tag,[])) for frame in warm]
   aggregate[tag][field]=statistics.median(values)
 rows.append(dict(log=path.name,warm_frames=4,phase_medians_ms=aggregate,postcommit_camera_qualification=postcommit))
(root/'warm-spans.json').write_text(json.dumps({'scope':'intrusive original baseline diagnosis, not latency qualification','alignment':'action delimiter; post-commit capture_camera_basis excluded and preserved separately','nested_spans':'do not add phase medians to reconstruct frame total','rows':rows},indent=2)+'\n')
print('Warm phase bundles:',len(rows))
