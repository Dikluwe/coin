from pathlib import Path
import subprocess,os,json,shutil,resource,xml.etree.ElementTree as E,hashlib
from PIL import Image,ImageDraw,ImageFont
r=Path(os.environ['COIN_CAPTURE_RAM']);out=Path(os.environ['COIN_CAPTURE_OUTPUT']);out.mkdir(exist_ok=True)
s=json.loads((r/'series.json').read_text());env=os.environ.copy();env['TMPDIR']=str(r/'tmp');env.pop('DISPLAY',None);resource.setrlimit(resource.RLIMIT_CORE,(0,0))
scenes={'B01':['transition-cube','transition-cylinder','transition-sphere','transition-default'],'B02':['vbo-coordinate3'],'B05':['normalmap-fallback'],'B06':['environment','reflectionmap-fallback'],'M07.02':['sphere-huge','sphere-tiny','sphere-control'],'M07.03':['bbox-cube','bbox-cylinder'],'M08.02':['bbox-bundle-gradient','bbox-bundle-reference','bundle-normal'],'M09':['triangle-picking'],'LIN':['line-picking'],'CUBE':['normalmap','reflectionmap'],'SPH':['normalmap-native','reflectionmap-native','legacy-default-native'],'ANA':['analytic-cube','analytic-cone','analytic-cylinder','analytic-vrmlsphere'],'VRML':['analytic-vrmlbox','analytic-vrmlcone','analytic-vrmlcylinder']}
(out/'scenes.cpp').write_bytes((r/'scenes.cpp').read_bytes());results=[]
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
heading=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',20)
def execute(args,log,e=env):
 with log.open('w') as f:p=subprocess.run(args,env=e,stdout=f,stderr=subprocess.STDOUT)
 return p.returncode
for x in s['items']:
 ident=x['id'];folder=out/ident;folder.mkdir(exist_ok=True)
 # Preserve the exact parent library before incrementally applying this delta.
 parent=r/'parent-lib';parent.mkdir(exist_ok=True)
 source=r/('base-build/lib/libCoin.so' if x['order']==1 else 'candidate-build/lib/libCoin.so')
 shutil.copy2(source.resolve(),parent/'libCoin.so.80.0.10')
 for n in ['libCoin.so','libCoin.so.80']:
  p=parent/n
  if not p.exists():p.symlink_to('libCoin.so.80.0.10')
 for line in subprocess.check_output(['git','diff','--name-status',x['base'],x['head']],text=True).splitlines():
  status,f=line.split('\t',1);p=r/'candidate-src'/f
  if status=='D':p.unlink();continue
  assert '\t' not in f
  p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','show',x['head']+':'+f]));p.touch()
 assert execute(['cmake','-S',str(r/'candidate-src'),'-B',str(r/'candidate-build')],r/(ident+'-configure.log'))==0
 assert execute(['cmake','--build',str(r/'candidate-build'),'--target','Coin',x['target'],'--parallel','8'],r/(ident+'-compile.log'))==0
 record=dict(id=ident,base=x['base'],head=x['head'],build_type='Debug -O0 -g0',no_coinrender=True,tests={},scenes=[])
 for side,lib in [('before',parent),('after',r/'candidate-build/lib')]:
  e=env.copy();e['LD_LIBRARY_PATH']=str(lib);j=folder/(side+'-tests.xml')
  args=['ctest','--test-dir',str(r/'candidate-build'),'-R',x['regex'],'--output-on-failure','--output-junit',str(j)]
  if ident.startswith('B'):
   e.update(LIBGL_ALWAYS_SOFTWARE='1',COIN_GLXGLUE_NO_PBUFFERS='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1');args=['xvfb-run','-a',*args]
  rc=execute(args,folder/(side+'-tests.log'),e);root=E.parse(j).getroot();cases=list(root.iter('testcase'))
  record['tests'][side]=dict(exit=rc,total=len(cases),failed=[c.get('name') for c in cases if c.find('failure') is not None],skipped=[c.get('name') for c in cases if c.find('skipped') is not None],mode='GLX under Xvfb' if ident.startswith('B') else 'CPU with DISPLAY unset')
  assert len(cases)>0 and not record['tests'][side]['skipped']
  assert (rc!=0 and record['tests'][side]['failed']) if side=='before' else rc==0,(ident,side,record['tests'][side])
  if 'CoinRender' in subprocess.check_output(['ldd',str(lib/'libCoin.so')],text=True):raise RuntimeError('CoinRender dependency')
  for scene in scenes.get(ident,[]):
   prefix=folder/(side+'-'+scene);e.update(LIBGL_ALWAYS_SOFTWARE='1',COIN_GLXGLUE_NO_PBUFFERS='1',COIN_GLX_PIXMAP_DIRECT_RENDERING='1',COIN_VBO='1',COIN_VBO_MIN_LIMIT='20')
   rc=execute(['xvfb-run','-a',str(r/'scenes'),scene,str(prefix)],folder/(side+'-'+scene+'.log'),e)
   if rc!=0:
    assert ident=='B01' and side=='before' and scene=='transition-default',(ident,side,scene,rc)
   else:
    Image.open(prefix.with_suffix('.ppm')).save(prefix.with_suffix('.png'));prefix.with_suffix('.ppm').unlink()
 for scene in scenes.get(ident,[]):
  a=folder/('before-'+scene);b=folder/('after-'+scene);row=dict(scene=scene)
  if a.with_suffix('.png').exists():
   imga=Image.open(a.with_suffix('.png'));imgb=Image.open(b.with_suffix('.png'))
   assert a.with_suffix('.iv').read_bytes()==b.with_suffix('.iv').read_bytes(),(ident,scene,'scene differs')
   ba=json.loads(a.with_suffix('.json').read_text());bb=json.loads(b.with_suffix('.json').read_text());assert ba['renderer']==bb['renderer'];row.update(different_pixels=sum(pa!=pb for pa,pb in zip(imga.getdata(),imgb.getdata())),frame_pixels=480*320,before_picks=ba['picks'],after_picks=bb['picks'],renderer=bb['renderer'],opengl=bb['version'])
   row['picking_changed']=ba['picks']!=bb['picks']
   panel=Image.new('RGB',(980,440),'#f5f6f8');d=ImageDraw.Draw(panel);d.text((10,8),ident+' | '+scene,fill='#151a22',font=heading)
   d.text((10,38),'BEFORE '+x['base'][:11],fill='black',font=font);d.text((500,38),'AFTER '+x['head'][:11],fill='black',font=font);panel.paste(imga,(10,65));panel.paste(imgb,(500,65))
   for dat,left in [(ba,10),(bb,500)]:
    pick=dat['picks'][0] if dat['picks'] else {};uv=pick.get('texture')
    if uv is not None:d.text((left,390),'Pick UV: ('+', '.join(format(v,'.6g') if isinstance(v,(int,float)) else str(v) for v in uv)+')',fill='black',font=font)
   d.text((10,415),str(row['different_pixels'])+' changed pixels / 153600; picking changed: '+str(row['picking_changed']),fill='black',font=font)
   panel.save(folder/(scene+'-comparison.png'))
  else:
   row['before_failure']='Debug abort before frame; see before-'+scene+'.log'
  record['scenes'].append(row)
 results.append(record);(out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
 print(ident,'baseline FAIL; candidate PASS;',len(record['scenes']),'scene pairs',flush=True)
print('DONE',len(results),'themes',flush=True)
