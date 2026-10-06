import hashlib,json,shutil,subprocess
from pathlib import Path
root=Path('/tmp/coin-render-first-frame');destination=Path('/tmp/coin-render-capture-baseline');assert not destination.exists()
controls={'wgpu':Path('/tmp/coin-render-matrix-final-binaries.json'),'bgfx':Path('/tmp/coin-render-validation-final-binaries.json')}
files=[];refs={'wgpu':'199b0e02b4f0b74ffb5d042d98e2837aafc31ad8','bgfx':'6182410f5789bbdc30d5ccd06fa341e1810aef8e'}
for family,c in controls.items():
 data=json.loads(c.read_text());selected=[f for f in data['files'] if f['role']==family];assert len(selected)==4
 for f in selected:
  p=Path(f['path']);assert hashlib.sha256(p.read_bytes()).hexdigest()==f['sha256'],p
  rel=p.relative_to(Path('/tmp/coin-render-first-frame-'+family));target=destination/family/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
  assert hashlib.sha256(target.read_bytes()).hexdigest()==f['sha256']
  files.append({'role':family,'path':str(target),'original_path':str(p),'source_content_revision':refs[family],'bytes':target.stat().st_size,'sha256':f['sha256']})
 for name in ('libCoin.so','libCoin.so.80'):(destination/family/'lib'/name).symlink_to('libCoin.so.80.0.10')
meta={'source_content_revision':refs['wgpu'],'source_snapshot_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'variant_source_content_revisions':{'wgpu-vulkan':refs['wgpu'],'bgfx-vulkan':refs['bgfx'],'bgfx-opengl':refs['bgfx']},'files':files,'hashes':{f['path']:f['sha256'] for f in files},'source_identity_note':'Before binaries have distinct compiled commits; per-variant map is authoritative.'}
Path('/tmp/coin-render-capture-baseline-binaries.json').write_text(json.dumps(meta,indent=2)+'\n')
print('Frozen and verified',len(files),'files')
