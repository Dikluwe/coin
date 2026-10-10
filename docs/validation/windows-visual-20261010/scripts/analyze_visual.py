import pathlib,json,re,hashlib,sys
import numpy as np
from PIL import Image
r=pathlib.Path(__file__).parent
out=r/sys.argv[1];rows=json.loads((out/'summary.json').read_text());analysis=[];pixels=[]
sha=lambda b:hashlib.sha256(b).hexdigest()
for row in rows:
 label=row['label'];a={'label':label,'process_gate':row['pass_gate'],'exit':row['exit'],'stages':{}}
 for stage in ['initial','final']:
  files=[out/(label+'-'+stage+'-'+kind+'.ppm') for kind in ['window','offscreen']]
  if all(p.exists() for p in files):
   imgs=[np.asarray(Image.open(p).convert('RGB')) for p in files];delta=np.abs(imgs[0].astype(np.int16)-imgs[1].astype(np.int16));mask=delta.max(axis=2)>0
   colors=imgs[0].astype(np.uint32);packed=(colors[:,:,0]<<16)|(colors[:,:,1]<<8)|colors[:,:,2]
   a['stages'][stage]={'rgb_max':int(delta.max()),'different_pixels':int(mask.sum()),'total_pixels':int(mask.size),'window_colors':int(len(np.unique(packed))),'equal':bool(not mask.any())}
   # Differences are displayed as a binary mask, not magnified source colors.
   Image.fromarray(mask.astype(np.uint8)*255).save(out/(label+'-'+stage+'-difference-mask.png'))
   for p,img in zip(files,imgs):
    png=p.with_suffix('.png');Image.fromarray(img).save(png);assert np.array_equal(np.asarray(Image.open(png)),img)
    pixels.append({'ppm':p.name,'ppm_sha256':sha(p.read_bytes()),'png':png.name,'png_sha256':sha(png.read_bytes()),'decoded_rgb_sha256':sha(img.tobytes()),'extent':list(Image.open(png).size)})
 first=out/(label+'-initial-window.ppm');last=out/(label+'-final-window.ppm')
 if first.exists() and last.exists():
  aa=np.asarray(Image.open(first));bb=np.asarray(Image.open(last));a['camera_changed_pixels']=int(np.any(aa!=bb,axis=2).sum())
 a['renderer_receipt']=re.findall(r'receipt .*', (out/row['log']).read_text(errors='replace'))
 analysis.append(a)
(out/'pixel-analysis.json').write_text(json.dumps(analysis,indent=2))
(out/'pixel-manifest.json').write_text(json.dumps(pixels,indent=2))
print(json.dumps({'processes':len(rows),'passes':sum(x['pass_gate'] for x in rows),'images':len(pixels),'per_workload':{w:{'processes':sum(x['label'].startswith(w+'-') for x in rows),'passes':sum(x['pass_gate'] and x['label'].startswith(w+'-') for x in rows)} for w in ['city-40000','terrain-million','solids','city-million']}},indent=2))
