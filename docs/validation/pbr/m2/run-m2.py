from pathlib import Path
import argparse, hashlib, json, os, subprocess
from PIL import Image, ImageChops, ImageDraw
p=argparse.ArgumentParser();p.add_argument('--viewer',default='/tmp/coin-pbr-bgfx/bin/coin_render_viewer');p.add_argument('--output',default='/tmp/coin-pbr-m2-matrix');a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.update(DISPLAY=':0',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',COIN_BGFX_RENDERER='vulkan')
rows=[]
def run(name,options):
    prefix=out/name;cmd=[a.viewer,'--pbr','--width','800','--height','600','--frames','2','--capture-prefix',str(prefix),*options]
    # Preserve the M2 direct-shading control with the later quality defaults.
    cmd+=['--single-scattering','--no-specular-filter']
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=60)
    (out/(name+'.log')).write_text(r.stdout+r.stderr)
    row=dict(case=name,command=cmd,exit_code=r.returncode,passed=False)
    if not r.returncode:
        images=[Image.open(f'{prefix}-{i:03d}.ppm').convert('RGB') for i in range(2)]
        meta=json.loads(Path(f'{prefix}-000.json').read_text())
        row.update(stable_pixels=ImageChops.difference(*images).getbbox() is None,pixels_sha256=hashlib.sha256(images[0].tobytes()).hexdigest(),renderer=meta['renderer'],vendor_id=meta['vendor_id'],profile=meta['profile'])
        row['passed']=row['stable_pixels'] and row['vendor_id']==0x1002 and row['renderer']==1 and row['profile']=='pbr-direct-m2'
        images[0].save(f'{prefix}-000.png')
    rows.append(row);print(name,'PASS' if row['passed'] else 'FAIL',flush=True);return row
for fixture in ['flange','assembly','housing']:
    for view in ['iso-perspective','iso-orthographic','front','top','right','detail']:
        run(f'{fixture}-{view}-offscreen',['--fixture',fixture,'--view',view,'--offscreen'])
    window=run(f'{fixture}-window',['--fixture',fixture,'--view','iso-perspective'])
    control=next(r for r in rows if r['case']==f'{fixture}-iso-perspective-offscreen')
    window['window_offscreen_equal']=window.get('pixels_sha256')==control.get('pixels_sha256')
    window['passed']=window['passed'] and window['window_offscreen_equal']
run('grid-top',['--fixture','grid','--view','top','--offscreen'])
run('housing-cut',['--fixture','housing','--view','iso-perspective','--cut','--offscreen'])
run('assembly-exposure-plus2',['--fixture','assembly','--view','iso-perspective','--exposure','2','--offscreen'])
for options in [['--exposure','nan'],['--exposure','17'],['--exposure','oops']]:
    r=subprocess.run([a.viewer,'--pbr','--offscreen',*options],env=env,capture_output=True,timeout=60)
    rows.append(dict(case='reject-'+options[-1],passed=r.returncode==2,exit_code=r.returncode))
selected=['flange-iso-perspective-offscreen','assembly-iso-perspective-offscreen','housing-iso-perspective-offscreen','grid-top','housing-cut','assembly-exposure-plus2']
gallery=Image.new('RGB',(1200,640),'#16202b');draw=ImageDraw.Draw(gallery)
for i,name in enumerate(selected):
    im=Image.open(out/f'{name}-000.png');im.thumbnail((400,300));x=i%3*400;y=i//3*320
    gallery.paste(im,(x,y));draw.text((x+8,y+301),name,fill='white')
gallery.save(out/'gallery.png')
(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
print('TOTAL',sum(r['passed'] for r in rows),'/',len(rows),flush=True)
raise SystemExit(0 if all(r['passed'] for r in rows) else 1)
