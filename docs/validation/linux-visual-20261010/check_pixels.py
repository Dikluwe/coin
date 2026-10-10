import hashlib,json,pathlib,sys
import numpy as np
from PIL import Image
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '/tmp/coin-linux-visual-results/final')
records=[];bad=[]
for backend in ('bgfx','wgpu'):
 for case in json.loads((root/backend/'results.json').read_text()):
  for stage,claimed_max,claimed_exact in case['oracle']:
   name=case['name'];p=root/backend/name
   files=[pathlib.Path(str(p)+f'-{stage}-{surface}.ppm') for surface in ('window','offscreen')]
   if not all(f.exists() for f in files):bad.append(f'missing pixels {name} {stage}');continue
   a=np.asarray(Image.open(files[0]).convert('RGB')).astype(np.int16);b=np.asarray(Image.open(files[1]).convert('RGB')).astype(np.int16)
   diff=np.abs(a-b);maximum=int(diff.max());pixels=int(np.any(diff!=0,axis=2).sum());coverage=int(np.any(a!=0,axis=2).sum())
   exact=(maximum==0 and coverage>a.shape[0]*a.shape[1]//10)
   rec={'name':name,'stage':stage,'max':maximum,'diff_pixels':pixels,'coverage':coverage,'exact':exact,'sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in files}}
   records.append(rec)
   if maximum!=int(claimed_max) or exact!=bool(int(claimed_exact)):bad.append(f'mismatch {name} {stage}: computed max={maximum},exact={exact}; claimed max={claimed_max},exact={claimed_exact}')
(root/'pixel-manifest.json').write_text(json.dumps({'records':records,'errors':bad},indent=2)+'\n')
print('checked',len(records),'stages,',len(bad),'errors')
for e in bad:print(e)
