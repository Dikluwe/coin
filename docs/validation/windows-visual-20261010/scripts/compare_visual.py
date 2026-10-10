import pathlib,json,sys
import numpy as np
from PIL import Image
r=pathlib.Path(__file__).parent
out=r/sys.argv[1];rows=json.loads((out/'summary.json').read_text());result=[]
for row in rows:
 if '-0-native' not in row['label']:continue
 for stage in ['initial','final']:
  pa=out/(row['label']+'-'+stage+'-window.ppm');pb=out/(row['label'].replace('-0-native','-1-portable')+'-'+stage+'-window.ppm')
  if pa.exists() and pb.exists():
   a=np.asarray(Image.open(pa));b=np.asarray(Image.open(pb));d=np.abs(a.astype(np.int16)-b.astype(np.int16))
   result.append({'pair':row['label'].replace('-0-native',''),'stage':stage,'rgb_max':int(d.max()),'different_pixels':int(np.any(d>0,axis=2).sum()),'kind':'diagnostic policy comparison; not an independent oracle or tolerance gate'})
(out/'policy-pixel-comparison.json').write_text(json.dumps(result,indent=2))
print('comparisons',len(result),'different',sum(v['different_pixels']>0 for v in result))
