import pathlib,json,hashlib,subprocess,sys,io
s=pathlib.Path(sys.argv[1]);d=s/'docs/validation/coin-render-clean-windows-20261015'
manifest=json.loads((d/'manifest.json').read_bytes())
paths=[]
for row in manifest:
 p=d/row['path'];raw=p.read_bytes()
 assert len(raw)==row['bytes'] and hashlib.sha256(raw).hexdigest()==row['sha256'],p
 paths.append(p.relative_to(s).as_posix())
if '--staged' in sys.argv:
 stream=io.BytesIO(subprocess.check_output(['git','-C',str(s),'cat-file','--batch'],input=''.join(':'+p+'\n' for p in paths).encode()))
 for row,p in zip(manifest,paths):
  info=stream.readline().split();assert len(info)==3 and info[1]==b'blob',(p,info)
  raw=stream.read(int(info[2]));assert stream.read(1)==b'\n'
  assert len(raw)==row['bytes'] and hashlib.sha256(raw).hexdigest()==row['sha256'],p
print('Verified',len(manifest),'evidence files;',sum(x['bytes'] for x in manifest),'bytes; staged=', '--staged' in sys.argv)
