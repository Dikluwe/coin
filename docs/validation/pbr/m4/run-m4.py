from pathlib import Path
import argparse,hashlib,json,os,subprocess,re
from PIL import Image,ImageChops,ImageDraw
p=argparse.ArgumentParser();p.add_argument('--viewer',default='/tmp/coin-pbr-bgfx/bin/coin_render_viewer');p.add_argument('--environments',default='/tmp/coin-pbr-m3-env');p.add_argument('--output',default='/tmp/coin-pbr-m4-matrix');p.add_argument('--cad-source',default='/home/dikluwe/Área de trabalho/Estudo coin/estudos/So/SoWgpu-FreeCAD-Exemplos/cenas/Assembly.iv');a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True);env=os.environ.copy();env.update(DISPLAY=':0',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',COIN_BGFX_RENDERER='vulkan',COIN_RENDER_TRACE_PHASES='1');rows=[]
views=['iso-perspective','iso-orthographic','front','top','right','detail']
def run(name,fixture,view,environment,options,frames=2,expected='pbr-cad-m4',size=(1280,960)):
 prefix=out/name;cmd=[a.viewer,'--fixture',fixture,'--view',view,'--width',str(size[0]),'--height',str(size[1]),'--frames',str(frames),'--capture-prefix',str(prefix),*options]
 if environment:cmd+=['--environment',str(Path(a.environments)/(environment+'.cibl')),'--direct-intensity','0']
 r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=90);(out/(name+'.log')).write_text(r.stdout+r.stderr)
 row=dict(case=name,command=cmd,passed=False,exit_code=r.returncode)
 if not r.returncode:
  ims=[Image.open(f'{prefix}-{i:03d}.ppm').convert('RGB') for i in range(frames)];metas=[json.loads(Path(f'{prefix}-{i:03d}.json').read_text()) for i in range(frames)]
  stable=frames!=2 or ImageChops.difference(*ims).getbbox() is None
  uploads=re.findall(r'pbr_environment id=(\w+) bytes=(\d+) uploads=(\d+) levels=(\d+)',r.stderr)
  hashes=[hashlib.sha256(im.tobytes()).hexdigest() for im in ims];probes=[m['depth_oracle_probes'] for m in metas]
  row.update(stable_pixels=stable,profile=metas[0]['profile'],pixels_sha256=hashes[0],frames=frames,unique_images=len(set(hashes)),depth_probes=probes,uploads=uploads,metadata=str(prefix)+'-000.json')
  depth_paths=[Path(f'{prefix}-{i:03d}.depth.f32') for i in range(frames)]
  if depth_paths[0].exists():row['depth_sha256']=hashlib.sha256(depth_paths[0].read_bytes()).hexdigest();row['stable_depth']=frames!=2 or depth_paths[0].read_bytes()==depth_paths[1].read_bytes()
  row['passed']=stable and row['profile']==expected and metas[0]['vendor_id']==0x1002 and (not environment or len(uploads)==1) and ('--validate-depth' not in options or min(probes)>100) and row.get('stable_depth',True)
  if '--cad-tools' in options:row['passed'] &= metas[0]['selected_diagonal']>0 and bool(metas[0]['selected_name'])
  if frames>2:row['passed'] &= len(set(hashes))==frames
  ims[0].save(f'{prefix}-000.png')
 rows.append(row);print(name,'PASS' if row['passed'] else 'FAIL',flush=True);return row
for environment in ['studio','asymmetric']:
 for fixture in ['flange','assembly','housing']:
  for view in views:
   for stops in [0,2]:run(f'{fixture}-{view}-{environment}-e{stops}',fixture,view,environment,['--offscreen','--exposure',str(stops),'--validate-depth','--capture-depth'])
for fixture in ['flange','assembly','housing']:
 for view in views:
  control=run(f'{fixture}-{view}-legacy-depth',fixture,view,None,['--offscreen','--capture-depth','--validate-depth'],expected='legacy-baseline')
  reference=next(r for r in rows if r['case']==f'{fixture}-{view}-studio-e0')
  control['pbr_geometry_depth_equal']=control.get('depth_sha256')==reference.get('depth_sha256');control['passed'] &= control['pbr_geometry_depth_equal']
for environment in ['studio','asymmetric']:
 run('grid-'+environment,'grid','top',environment,['--offscreen','--validate-depth','--capture-depth'])
 for fixture in ['flange','assembly','housing']:
  run(f'{fixture}-tools-{environment}',fixture,'iso-perspective',environment,['--offscreen','--cad-tools','--validate-depth','--capture-depth'])
 run('housing-cut-'+environment,'housing','iso-perspective',environment,['--offscreen','--cut','--cad-tools','--validate-depth','--capture-depth'])
 window=run('housing-window-'+environment,'housing','iso-perspective',environment,[])
 ref=next(r for r in rows if r['case']==f'housing-iso-perspective-{environment}-e0');window['window_offscreen_equal']=window.get('pixels_sha256')==ref.get('pixels_sha256');window['passed'] &= window['window_offscreen_equal']
run('assembly-orbit-studio','assembly','iso-perspective','studio',['--offscreen','--orbit-frames','36','--validate-depth','--capture-depth'],frames=36,size=(800,600))
run('assembly-orbit-asymmetric','assembly','iso-perspective','asymmetric',['--offscreen','--orbit-frames','36','--validate-depth','--capture-depth'],frames=36,size=(800,600))
if Path(a.cad_source).exists():
 run('freecad-legacy','assembly','iso-perspective',None,['--offscreen','--scene',a.cad_source,'--capture-depth','--validate-depth'],expected='legacy-baseline')
 run('freecad-pbr','assembly','iso-perspective','studio',['--offscreen','--scene',a.cad_source,'--convert-import','--capture-depth','--validate-depth','--export-scene',str(out/'freecad-pbr.iv')])
 (out/'cad-source.json').write_text(json.dumps(dict(path=a.cad_source,sha256=hashlib.sha256(Path(a.cad_source).read_bytes()).hexdigest(),converted_sha256=hashlib.sha256((out/'freecad-pbr.iv').read_bytes()).hexdigest()),indent=2)+'\n')
selected=[f'{fixture}-iso-perspective-{environment}-e0' for environment in ['studio','asymmetric'] for fixture in ['flange','assembly','housing']]
gallery=Image.new('RGB',(1200,640),'#16202b');draw=ImageDraw.Draw(gallery)
for i,name in enumerate(selected):
 path=out/f'{name}-000.png'
 if not path.exists():continue
 im=Image.open(path);im.thumbnail((400,300));x=i%3*400;y=i//3*320;gallery.paste(im,(x,y));draw.text((x+8,y+301),name,fill='white')
gallery.save(out/'gallery.png');(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print('TOTAL',sum(r['passed'] for r in rows),'/',len(rows),flush=True);raise SystemExit(0 if all(r['passed'] for r in rows) else 1)
