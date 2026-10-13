from pathlib import Path
import argparse, hashlib, json, os, subprocess, re
from PIL import Image,ImageChops,ImageDraw
p=argparse.ArgumentParser();p.add_argument('--viewer',default='/tmp/coin-pbr-bgfx/bin/coin_render_viewer');p.add_argument('--environments',default='/tmp/coin-pbr-m3-env');p.add_argument('--output',default='/tmp/coin-pbr-m3-matrix');a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True);env=os.environ.copy();env.update(DISPLAY=':0',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',COIN_BGFX_RENDERER='vulkan',COIN_RENDER_TRACE_PHASES='1')
rows=[];bundles={}
for name in ['studio','asymmetric']:
    path=Path(a.environments)/f'{name}.cibl';manifest=json.loads(path.with_suffix('.cibl.json').read_text())
    bundles[name]=dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),content_id=manifest['content_id_fnv1a64'],payload_bytes=manifest['payload_bytes'])
def run(name,fixture,view,environment,options):
    prefix=out/name;b=bundles[environment]
    cmd=[a.viewer,'--fixture',fixture,'--view',view,'--environment',b['path'],'--direct-intensity','0','--width','800','--height','600','--frames','2','--capture-prefix',str(prefix),*options]
    # Keep the M3 interaction-only control reproducible with the M4/M5 viewer.
    cmd+=['--single-scattering','--no-specular-filter']
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=60);(out/f'{name}.log').write_text(r.stdout+r.stderr)
    row=dict(case=name,command=cmd,passed=False,exit_code=r.returncode,environment=environment)
    if not r.returncode:
        images=[Image.open(f'{prefix}-{i:03d}.ppm').convert('RGB') for i in range(2)];meta=json.loads(Path(f'{prefix}-000.json').read_text())
        uploads=re.findall(r'pbr_environment id=(\w+) bytes=(\d+) uploads=(\d+) levels=(\d+)',r.stderr)
        row.update(stable_pixels=ImageChops.difference(*images).getbbox() is None,pixels_sha256=hashlib.sha256(images[0].tobytes()).hexdigest(),profile=meta['profile'],uploads=uploads,vendor_id=meta['vendor_id'],renderer=meta['renderer'])
        row['passed']=row['stable_pixels'] and row['profile']=='pbr-ibl-m3' and row['vendor_id']==0x1002 and row['renderer']==1 and len(uploads)==1 and uploads[0][0]==b['content_id'] and uploads[0][2]=='1'
        images[0].save(f'{prefix}-000.png')
    rows.append(row);print(name,'PASS' if row['passed'] else 'FAIL',flush=True);return row
for environment in bundles:
    for fixture in ['flange','assembly','housing']:
        for view in ['iso-perspective','iso-orthographic']:
            run(f'{fixture}-{view}-{environment}',fixture,view,environment,['--offscreen'])
    window=run(f'assembly-window-{environment}','assembly','iso-perspective',environment,[])
    reference=next(r for r in rows if r['case']==f'assembly-iso-perspective-{environment}')
    window['window_offscreen_equal']=window.get('pixels_sha256')==reference.get('pixels_sha256');window['passed']=window['passed'] and window['window_offscreen_equal']
    run(f'grid-{environment}','grid','top',environment,['--offscreen'])
    rotated=run(f'assembly-rotation90-{environment}','assembly','iso-perspective',environment,['--offscreen','--env-rotation','90'])
    rotated['rotation_changes_pixels']=rotated.get('pixels_sha256')!=reference.get('pixels_sha256');rotated['passed']=rotated['passed'] and rotated['rotation_changes_pixels']
    brighter=run(f'assembly-intensity2-{environment}','assembly','iso-perspective',environment,['--offscreen','--env-intensity','2'])
    brighter['intensity_changes_pixels']=brighter.get('pixels_sha256')!=reference.get('pixels_sha256');brighter['passed']=brighter['passed'] and brighter['intensity_changes_pixels']
for name,options in [('missing-bundle',['--environment',str(out/'missing.cibl')]),('invalid-intensity',['--environment',bundles['studio']['path'],'--env-intensity','-1'])]:
    r=subprocess.run([a.viewer,'--offscreen','--frames','1',*options],env=env,capture_output=True,timeout=60)
    rows.append(dict(case='reject-'+name,passed=r.returncode!=0,exit_code=r.returncode))
selected=[f'{fixture}-iso-perspective-{environment}' for environment in bundles for fixture in ['flange','assembly','housing']]
gallery=Image.new('RGB',(1200,640),'#16202b');draw=ImageDraw.Draw(gallery)
for i,name in enumerate(selected):
    im=Image.open(out/f'{name}-000.png');im.thumbnail((400,300));x=i%3*400;y=i//3*320;gallery.paste(im,(x,y));draw.text((x+8,y+301),name,fill='white')
gallery.save(out/'gallery.png');(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');(out/'environments.json').write_text(json.dumps(bundles,indent=2)+'\n')
print('TOTAL',sum(r['passed'] for r in rows),'/',len(rows),flush=True);raise SystemExit(0 if all(r['passed'] for r in rows) else 1)
