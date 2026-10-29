from pathlib import Path
import json,numpy as np,hashlib,statistics
from PIL import Image
p=Path('/tmp/coin-retained-million-ab'); runs=json.loads((p/'results.json').read_text()); assert len(runs)==4
assert len(set(r['camera_and_viewport'] for r in runs))==1
base=p/'2-current'; comparisons=[]
for r in runs:
 d=Path(r['directory']); a=np.fromfile(base/'frame.rgba',np.uint8);b=np.fromfile(d/'frame.rgba',np.uint8)
 x=np.fromfile(base/'frame.depth',np.float32);y=np.fromfile(d/'frame.depth',np.float32)
 diff=np.abs(a.astype(np.int16)-b.astype(np.int16));dd=np.abs(x-y)
 comparisons.append(dict(trial=r['trial'],mode=r['mode'],rgba_max_delta=int(diff.max()),rgba_different_channels=int(np.count_nonzero(diff)),depth_max_delta=float(dd.max()),depth_different_values=int(np.count_nonzero(dd)),depth_nonfinite=int(np.count_nonzero(~np.isfinite(y)))))
 Image.fromarray(b.reshape(2400,1080,4)).save(d/'capture.png')
summary={}
for mode in ['current','retained']:
 rows=[r for r in runs if r['mode']==mode]
 summary[mode]={k:statistics.median(r[k] for r in rows) for k in ['first_render_ms','steady_median_ms','steady_p90_ms','sampled_peak_rss_kib']}
 summary[mode]['memory']={k:statistics.median(r['median_memory'][k] for r in rows) for k in rows[0]['median_memory']}
files={str(f.relative_to(p)):hashlib.sha256(f.read_bytes()).hexdigest() for r in runs for f in Path(r['directory']).glob('frame.*')}
files['coin-render-million-ab.apk']=hashlib.sha256((p/'coin-render-million-ab.apk').read_bytes()).hexdigest()
result=dict(summary=summary,equivalence=comparisons,sha256=files)
(p/'analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
