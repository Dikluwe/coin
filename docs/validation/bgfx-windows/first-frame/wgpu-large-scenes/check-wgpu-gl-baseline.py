import json
import os
import shutil
import subprocess
from pathlib import Path

root = Path('H:/Git/coin')
baseline = root / 'build/wgpu-large-baseline'
inventory = json.loads(subprocess.check_output(['ctest', '--test-dir', str(root / 'build/coin-render-wgpu-msvc'), '-C', 'Release', '--show-only=json-v1'], text=True))
names = (root / 'build/wgpu-large-cross-api-tests.txt').read_text(encoding='utf-8-sig').splitlines()
names.remove('CoinWgpuLargeBindingsTest')
results = []
env = dict(os.environ, WGPU_BACKEND='gl', COIN_RENDER_REQUIRE_GL_REFERENCE='1', COIN_WGPU_REQUIRE_GL_REFERENCE='1', COIN_WGPU_CAMERA_BINDINGS='1')
for test in inventory['tests']:
    if test['name'] not in names:
        continue
    command = list(test['command'])
    exe = Path(command[0])
    shutil.copy2(exe, baseline / exe.name)
    command[0] = str(baseline / exe.name)
    log = root / f"build/wgpu-gl-baseline-{test['name']}.log"
    with log.open('w', encoding='utf-8') as output:
        result = subprocess.run(command, cwd=root, env=env, stdout=output, stderr=subprocess.STDOUT, timeout=120)
    results.append({'name': test['name'], 'command': command, 'exit_code': result.returncode, 'log': str(log.relative_to(root))})
    print(test['name'], 'exit=', result.returncode, flush=True)
    (root / 'build/wgpu-gl-baseline-tests.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
