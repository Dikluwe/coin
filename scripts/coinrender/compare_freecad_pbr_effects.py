#!/usr/bin/env python3
"""Replay a recorded native effects preview through independent offscreen controls.

Generate the input directory with freecad_assembly_appearance.FCMacro and
COIN_ASSEMBLY_PREVIEW_OUTPUT. Native OS snapshots are test evidence only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--preview', type=Path, required=True)
    args = parser.parse_args()
    preview = args.preview.resolve()
    result = json.loads((preview / 'results.json').read_text())
    assert result['status'] == 'PASS' and len(result['records']) == 9
    session = preview / 'industrial-session'
    requests = (session / 'requests.txt').read_text() + 'QUIT\n'
    frames = [json.loads(line) for line in (session / 'frames.jsonl').read_text().splitlines()]
    first = frames[0]
    for mode in ('current', 'retained'):
        output = preview / (mode + '-control')
        output.mkdir()  # Never reuse old control buffers.
        command = [str(args.worker.resolve()), str(session / 'scene.iv'), str(args.environment.resolve()),
                   str(output), '13', str(first['width']), str(first['height'])]
        if mode == 'current':
            command.append('--current')
        process = subprocess.run(command, input=requests, text=True, capture_output=True, timeout=120)
        (preview / (mode + '-control.log')).write_text(process.stdout + process.stderr)
        assert process.returncode == 0, process.stderr
        replies = [json.loads(line) for line in process.stdout.splitlines()]
        assert len(replies) == len(frames) and all(r['status'] == 'ok' for r in replies), replies
    comparisons = []
    for frame in frames:
        for suffix in ('rgba', 'depth'):
            name = f"frame-{frame['id']}.{suffix}"
            actual = (preview / 'retained-control' / name).read_bytes()
            assert actual == (preview / 'current-control' / name).read_bytes(), name
            comparisons.append(dict(file=name, bytes=len(actual), sha256=hashlib.sha256(actual).hexdigest()))
    native = []
    for record in result['records']:
        assert record['pixel_readbacks'] == record['cpu_pixel_bytes'] == record['qt_image_bytes'] == record['legacy_render_calls'] == 0
        actual = (preview / (record['stage'] + '.rgba')).read_bytes()
        expected = (preview / 'current-control' / f"frame-{record['id']}.rgba").read_bytes()
        assert len(actual) == len(expected)
        differences = [abs(a-b) for a,b in zip(actual, expected)]
        native.append(dict(stage=record['stage'], equal=actual == expected,
                           mean_absolute_channel_error=sum(differences)/len(differences),
                           maximum_channel_error=max(differences),
                           sha256=hashlib.sha256(actual).hexdigest()))
    controls = preview / 'retained-control'
    depth = (controls / f"frame-{result['records'][0]['id']}.depth").read_bytes()
    for record in result['records'][:5]:
        assert depth == (controls / f"frame-{record['id']}.depth").read_bytes(), 'Effect changed geometry depth'
        assert all(record[k] == 1 for k in ('captures', 'plans', 'transport_builds', 'environment_uploads'))
    result.update(exact_offscreen_comparisons=comparisons, native_control_differences=native)
    result['sha256'] = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name,path in
                        [('worker', args.worker), ('environment', args.environment), ('scene', session/'scene.iv')]}
    (preview / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f"PASS: {len(comparisons)} exact offscreen color/depth buffers; {len(native)} native image comparisons recorded")


if __name__ == '__main__':
    main()
