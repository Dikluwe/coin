#!/usr/bin/env python3
"""Read-only post campaign hashes and hardware observations."""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess

BASE = Path('/tmp')
PREFIX = 'coin-render-material-table'

def read(path):
    return json.loads(path.read_text())

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save(suffix, value):
    path = BASE / (PREFIX + suffix)
    if path.exists():
        raise RuntimeError('Preserve existing post observation: ' + str(path))
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + '\n')

now = datetime.datetime.now(datetime.timezone.utc).isoformat()
files, cards = [], []
for stage, name in [('baseline', PREFIX + '-baseline-binaries.json'),
                    ('after', PREFIX + '-after-binaries.json'),
                    ('coingl', 'coin-render-state-coingl-binaries.json')]:
    card_path = BASE / name
    card = read(card_path)
    cards.append({'stage': stage, 'path': str(card_path), 'sha256': digest(card_path)})
    for item in card['files']:
        path = Path(item['path'])
        actual, size = digest(path), path.stat().st_size
        unchanged = actual == item['sha256'] and ('bytes' not in item or size == item['bytes'])
        files.append({'stage': stage, 'path': str(path), 'bytes': size,
                      'sha256': item['sha256'], 'post_sha256': actual, 'unchanged': unchanged,
                      'before_size_recorded': 'bytes' in item,
                      'source_content_revision': item.get('source_content_revision', card.get('source_content_revision'))})
assert len(files) == len({(x['stage'], x['path']) for x in files}) == 20
assert all(x['unchanged'] for x in files)
save('-binary-hashes-post.json', {'recorded_at_utc': now, 'cards': cards, 'files': files, 'all_unchanged': True,
     'limitation': 'CoinGL before card pins hashes only; its sizes are observed in this post snapshot.'})

card_path = BASE / 'coin-render-material-slot-scenes.json'
card = read(card_path)
expected = {item['scene']['path']: item['scene'] for item in card['scenes']}
original = card['scenes'][0]['source_city']
expected[original['path']] = original
files = []
for name, item in expected.items():
    path = Path(name)
    actual, size = digest(path), path.stat().st_size
    unchanged = actual == item['sha256'] and size == item['bytes']
    files.append({'path': name, 'bytes': size, 'sha256': item['sha256'],
                  'post_sha256': actual, 'unchanged': unchanged})
assert len(files) == 4 and all(x['unchanged'] for x in files)
save('-scenes-post.json', {'recorded_at_utc': now, 'card': {'path': str(card_path), 'sha256': digest(card_path)},
     'files': files, 'all_unchanged': True,
     'scope': 'Independent coordinator check after all GPU runtime; executed runner manifests are unmodified.'})

before = read(BASE / (PREFIX + '-hardware-before.json'))
commands = []
for item in before['commands']:
    completed = subprocess.run(item['command'], capture_output=True, text=True)
    commands.append({'command': item['command'], 'exit_code': completed.returncode,
                     'stdout': completed.stdout, 'stderr': completed.stderr})
governors = {str(path): path.read_text().strip()
             for path in sorted(Path('/sys/devices/system/cpu').glob('cpu[0-9]*/cpufreq/scaling_governor'))}
save('-hardware-after.json', {'recorded_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
     'commands': commands, 'cpu_governors': governors,
     'limitation': 'Read-only endpoint observations; no clock/governor/driver settings changed. ps cpu values are process lifetime averages. These endpoints do not establish constant clocks during the campaigns.'})
print('PASS: 20 binary hashes, 4 scene hashes; read-only hardware endpoint saved.')
