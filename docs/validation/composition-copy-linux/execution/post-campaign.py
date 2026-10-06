#!/usr/bin/env python3
"""Pin binaries and record source/hardware integrity for the composition campaign."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess

ROOT = Path('/tmp/coin-render-first-frame')
PREFIX = '/tmp/coin-render-composition-copy'
CONTROL = '4d63bb993022ee8d40802558b0871a4803002b8d'

def read(path):
    return json.loads(Path(path).read_text())

def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n')

def revision():
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def hardware(side):
    commands = [['uname', '-a'], ['lscpu'],
        ['nvidia-smi', '--query-gpu=name,driver_version,pstate,temperature.gpu,clocks.current.graphics,clocks.current.memory', '--format=csv,noheader'],
        ['xset', 'q']]
    result = {'timestamp_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(), 'commands': []}
    governor = Path('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor')
    result['cpu_governor'] = governor.read_text().strip() if governor.exists() else None
    for command in commands:
        run = subprocess.run(command, capture_output=True, text=True, timeout=30)
        result['commands'].append({'command': command, 'exit_code': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr})
    write(PREFIX+'-hardware-'+side+'.json', result)
    print(result['commands'][2]['stdout'].strip())

def pin():
    rev = revision()
    files = []
    for role in ('wgpu', 'bgfx'):
        build = Path('/tmp/coin-render-first-frame-'+role)
        for relative in ('bin/coin_render_gl_benchmark', 'bin/coin_render_window_benchmark', 'lib/libCoin.so.80.0.10', 'lib/libCoinRender.so'):
            path = build/relative
            files.append({'role': role, 'path': str(path), 'source_content_revision': rev, 'bytes': path.stat().st_size, 'sha256': sha(path)})
    write(PREFIX+'-final-binaries.json', {'source_content_revision': rev, 'source_snapshot_revision': rev,
        'variant_source_content_revisions': {v: rev for v in ('wgpu-vulkan','bgfx-vulkan','bgfx-opengl')},
        'files': files, 'hashes': {f['path']: f['sha256'] for f in files}})
    print('Pinned', len(files), 'files at', rev)

def post():
    rev = revision()
    path = Path(PREFIX+'-verify-after/manifest.json')
    manifest = read(path)
    sources = {v: rev for v in ('wgpu-vulkan','bgfx-vulkan','bgfx-opengl')}
    sources['coingl'] = CONTROL
    manifest.update(source_snapshot_revision=manifest['source_revision'], source_content_revision=rev,
        variant_source_content_revisions=sources, source_content_revision_kind='single-render-family',
        source_identity_note='CoinRender binaries share the pinned production source. CoinGL is the separately pinned control.')
    for command in manifest['commands']:
        variant = next(v for v in sources if '-'+v+'-' in command['stem'])
        command.update(variant=variant, source_content_revision=sources[variant])
    write(path, manifest)
    files = []
    for stage, metadata in (('baseline', PREFIX+'-baseline-binaries.json'), ('final', PREFIX+'-final-binaries.json'), ('coingl', '/tmp/coin-render-state-coingl-binaries.json')):
        for item in read(metadata)['files']:
            files.append(dict(item, stage=stage, post_sha256=sha(item['path'])))
    assert len(files) == len({f['path'] for f in files}) == 20
    for item in files:
        item['unchanged'] = item['sha256'] == item['post_sha256']
    assert all(f['unchanged'] for f in files)
    write(PREFIX+'-binary-hashes-post.json', {'all_unchanged': True, 'files': files})
    hardware('after')
    print('Verified all 20 binary hashes unchanged')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('hardware-before', 'pin', 'post'))
    action = parser.parse_args().action
    if action == 'hardware-before': hardware('before')
    elif action == 'pin': pin()
    else: post()
