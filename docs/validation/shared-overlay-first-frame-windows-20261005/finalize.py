import json
from pathlib import Path

out=Path('H:/Git/coin/build/shared-overlay-first-frame-windows-20261005')
summary=json.loads((out/'summary.json').read_text())
installed=json.loads((out/'installed-hashes.json').read_text(encoding='utf-8-sig'))
for backend in ('bgfx','wgpu'):
    assert installed[backend]['binary_sha256']==summary['qualified_binary_hashes'][backend],backend
summary['installed']=installed
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print('Installed hashes match qualified binaries.')
