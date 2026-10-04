from pathlib import Path
import json,statistics
import numpy as np
from PIL import Image,ImageDraw,ImageFont
out=Path('/tmp/coin-render-bgfx-300-results')
rows=json.loads((out/'results.json').read_text())
medians={v:{key:statistics.median(r[key] for r in rows if r['variant']==v) for key in ['first_ms','median_ms','p95_ms','result_since_main_ms','peak_rss_kib']} for v in dict.fromkeys(r['variant'] for r in rows)}
comparison=[];rgb=[]
for base in ['bgfx-vulkan','bgfx-opengl','wgpu-vulkan']:
 a,b=medians[base+'-after'],medians[base+'-before'];ref=medians['coingl-nvidia']
 comparison.append(dict(backend=base,before=b,after=a,coingl=ref,first_change_percent=100*(a['first_ms']/b['first_ms']-1),hot_change_percent=100*(a['median_ms']/b['median_ms']-1),first_vs_coingl_percent=100*(a['first_ms']/ref['first_ms']-1),hot_speedup_vs_coingl=ref['median_ms']/a['median_ms']))
 for reference in [base+'-before','coingl-nvidia']:
  ia=np.asarray(Image.open(out/(base+'-after-1.ppm')),dtype=np.int16)
  ib=np.asarray(Image.open(out/(reference+'-1.ppm')),dtype=np.int16)
  assert ia.shape==ib.shape==(1024,1024,3)
  diff=np.abs(ia-ib)
  rgb.append(dict(variant=base+'-after',reference=reference,mae=float(diff.mean()),max=int(diff.max()),changed_pixels=int(np.any(diff>0,axis=2).sum()),pixels_over3=int(np.any(diff>3,axis=2).sum())))
  if reference.endswith('-before'):assert diff.max()==0
for name,data in [('comparison.json',comparison),('medians.json',medians),('rgb.json',rgb)]:
 (out/name).write_text(json.dumps(data,indent=2)+'\n')
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
paths=['coingl-nvidia','bgfx-vulkan-after','bgfx-opengl-after','wgpu-vulkan-after']
sheet=Image.new('RGB',(384*len(paths),420),'#eeeeee');draw=ImageDraw.Draw(sheet)
for i,variant in enumerate(paths):
 frame=Image.open(out/(variant+'-1.ppm')).convert('RGB');frame.thumbnail((384,384))
 sheet.paste(frame,(i*384,36));draw.text((i*384+12,8),variant,fill='#111111',font=font)
sheet.save(out/'nvidia-rgb-comparison.png')
print(json.dumps(comparison,indent=2))
