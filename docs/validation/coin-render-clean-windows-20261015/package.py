import pathlib,sys,json,hashlib,shutil
from PIL import Image
r=pathlib.Path(__file__).parent;s=pathlib.Path(sys.argv[1]);out=s/'docs/validation/coin-render-clean-windows-20261015';out.mkdir(parents=True,exist_ok=True)
pixels=[]
for p in r.glob('*.ppm'):
 im=Image.open(p).convert('RGB');png=r/(p.stem+'.png');im.save(png);assert Image.open(png).convert('RGB').tobytes()==im.tobytes()
 pixels.append({'ppm':p.name,'ppm_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'png':png.name,'rgb_sha256':hashlib.sha256(im.tobytes()).hexdigest(),'extent':list(im.size)})
(r/'pixel-manifest.json').write_text(json.dumps(pixels,indent=2))
for p in r.iterdir():
 if p.is_file() and p.suffix in ['.py','.log','.json','.xml','.txt','.diff','.png']:
  shutil.copy2(p,out/p.name)
for name in ['initial-source','headers-src']:
 if (r/name).exists():shutil.copytree(r/name,out/name,dirs_exist_ok=True)
(out/'.gitattributes').write_text('* -text\n')
manifest=[{'path':p.relative_to(out).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.rglob('*')) if p.is_file() and p.name!='manifest.json']
(out/'manifest.json').write_text(json.dumps(manifest,indent=2));print('evidence',len(manifest),'files',sum(x['bytes'] for x in manifest),'bytes')
