from pathlib import Path
import json,statistics
import numpy as np
from PIL import Image,ImageDraw,ImageFont
out=Path('/tmp/coin-render-coingl-reference-results')
rows=json.loads((out/'results.json').read_text())
medians={v:{key:statistics.median(r[key] for r in rows if r['variant']==v) for key in ['first_ms','median_ms','p95_ms','result_since_main_ms','peak_rss_kib']} for v in dict.fromkeys(r['variant'] for r in rows)}
comparison=[]
for variant in ['bgfx-vulkan','bgfx-opengl','wgpu-vulkan','wgpu-opengl']:
 ref='coingl-amd' if variant=='wgpu-opengl' else 'coingl-nvidia'
 a,b=medians[variant],medians[ref]
 ia=np.asarray(Image.open(out/(variant+'-1.ppm')).convert('RGB'),dtype=np.int16)
 ib=np.asarray(Image.open(out/(ref+'-1.ppm')).convert('RGB'),dtype=np.int16)
 assert ia.shape==ib.shape==(1024,1024,3)
 diff=np.abs(ia-ib)
 visual={'rgb_mae_0_255':float(diff.mean()),'rgb_rmse_0_255':float(np.sqrt((diff.astype(np.float64)**2).mean())),'max_channel_error':int(diff.max()),'different_pixels_percent':float(100*np.any(diff>0,axis=2).mean()),'pixels_channel_error_gt_3_percent':float(100*np.any(diff>3,axis=2).mean())}
 comparison.append(dict(variant=variant,reference=ref,medians=a,reference_medians=b,first_ratio_to_coingl=a['first_ms']/b['first_ms'],steady_ratio_to_coingl=a['median_ms']/b['median_ms'],steady_speedup=b['median_ms']/a['median_ms'],rgb_comparison=visual))
 print(variant,'first',a['first_ms'],'hot',a['median_ms'],'vsCoinGL first',a['first_ms']/b['first_ms'],'steady speedup',b['median_ms']/a['median_ms'],'RGB MAE',visual['rgb_mae_0_255'])
(out/'comparison.json').write_text(json.dumps(comparison,indent=2)+'\n')
(out/'medians.json').write_text(json.dumps(medians,indent=2)+'\n')
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
for gpu,paths in [('nvidia',['coingl-nvidia','bgfx-vulkan','bgfx-opengl','wgpu-vulkan']),('amd',['coingl-amd','wgpu-opengl'])]:
 sheet=Image.new('RGB',(384*len(paths),420),'#eeeeee');draw=ImageDraw.Draw(sheet)
 for i,variant in enumerate(paths):
  frame=Image.open(out/(variant+'-1.ppm')).convert('RGB');frame.thumbnail((384,384))
  sheet.paste(frame,(i*384,36));draw.text((i*384+12,8),variant,fill='#111111',font=font)
 sheet.save(out/(gpu+'-rgb-comparison.png'))
