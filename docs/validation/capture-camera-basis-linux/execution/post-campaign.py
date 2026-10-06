#!/usr/bin/env python3
"""Record post-campaign binary integrity, variant sources and hardware."""
import hashlib,json,subprocess
from pathlib import Path
root=Path('/tmp/coin-render-first-frame')
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
p=Path('/tmp/coin-render-capture-verify-after/manifest.json');m=json.loads(p.read_text())
sources={v:revision for v in ('wgpu-vulkan','bgfx-vulkan','bgfx-opengl')}
sources['coingl']='4d63bb993022ee8d40802558b0871a4803002b8d'
m.update(source_snapshot_revision=m['source_revision'],source_content_revision=revision,
    variant_source_content_revisions=sources,source_content_revision_kind='single-render-family',
    source_identity_note='CoinRender binaries share current source; CoinGL retains its separately pinned control revision.')
for c in m['commands']:
 v=next(v for v in sources if '-'+v+'-' in c['stem']);c.update(variant=v,source_content_revision=sources[v])
p.write_text(json.dumps(m,indent=2)+'\n')
items=[]
for name in ('baseline','final'):
 meta=json.loads(Path('/tmp/coin-render-capture-'+name+'-binaries.json').read_text())
 for f in meta['files']:items.append(dict(f,stage=name))
meta=json.loads(Path('/tmp/coin-render-state-coingl-binaries.json').read_text())
for f in meta['files']:items.append(dict(f,stage='coingl'))
assert len(items)==20 and len({f['path'] for f in items})==20
for f in items:
 f['post_sha256']=hashlib.sha256(Path(f['path']).read_bytes()).hexdigest();f['unchanged']=f['post_sha256']==f['sha256']
result={'all_unchanged':all(f['unchanged'] for f in items),'files':items}
assert result['all_unchanged']
Path('/tmp/coin-render-capture-binary-hashes-post.json').write_text(json.dumps(result,indent=2)+'\n')
h=json.loads(Path('/tmp/coin-render-capture-hardware-before.json').read_text())
for item in h['commands']:
 c=item['command'];x=subprocess.run(c,capture_output=True,text=True);item.update(exit_code=x.returncode,stdout=x.stdout,stderr=x.stderr)
Path('/tmp/coin-render-capture-hardware-after.json').write_text(json.dumps(h,indent=2)+'\n')
print('Verified all 20 binary hashes unchanged')
print(h['commands'][2]['stdout'])
