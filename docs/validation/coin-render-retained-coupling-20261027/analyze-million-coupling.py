from pathlib import Path
import json,hashlib,statistics,struct,numpy as np
from PIL import Image
p=Path('/tmp/coin-retained-million-coupling');runs=json.loads((p/'results.json').read_text());assert len(runs)==6
assert len({r['camera_and_viewport'] for r in runs})==1
base=p/'1-current';a=np.fromfile(base/'frame.rgba',np.uint8);x=np.fromfile(base/'frame.depth',np.float32)
window=np.array(Image.open(base/'display.png'));comparisons=[];hashes={}
for r in runs:
 d=Path(r['directory']);b=np.fromfile(d/'frame.rgba',np.uint8);y=np.fromfile(d/'frame.depth',np.float32)
 diff=np.abs(a.astype(np.int16)-b.astype(np.int16));dd=np.abs(x-y);w=np.array(Image.open(d/'display.png'))
 comparisons.append(dict(trial=r['trial'],mode=r['mode'],rgba_max_delta=int(diff.max()),rgba_different_channels=int(np.count_nonzero(diff)),depth_max_delta=float(dd.max()),depth_different_values=int(np.count_nonzero(dd)),depth_nonfinite=int(np.count_nonzero(~np.isfinite(y))),display_different_channels_excluding_100_top_bottom=int(np.count_nonzero(window[100:-100]!=w[100:-100]))))
 Image.fromarray(b.reshape(2400,1080,4)).save(d/'capture.png')
 for f in d.glob('frame.*'):hashes[str(f.relative_to(p))]=hashlib.sha256(f.read_bytes()).hexdigest()
summary={}
for mode in ['current','retained']:
 rows=[r for r in runs if r['mode']==mode]
 summary[mode]={k:statistics.median(r[k] for r in rows) for k in ['first_render_ms','steady_median_ms','steady_p90_ms','sampled_peak_rss_kib']}
 summary[mode]['memory']={k:statistics.median(r['median_memory'][k] for r in rows) for k in rows[0]['median_memory']}
for f in ['coin-render-million-ab.apk']:hashes[f]=hashlib.sha256((p/f).read_bytes()).hexdigest()
result=dict(summary=summary,equivalence=comparisons,sha256=hashes);(p/'analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
