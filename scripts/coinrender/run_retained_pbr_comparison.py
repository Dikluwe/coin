#!/usr/bin/env python3
"""Run PBR controls serially and compare exact RGBA/depth; no timing qualification."""
import argparse
import hashlib
import json
import os
import math
import struct
from pathlib import Path
import subprocess


def compare(root, names):
    count = 0
    hashes = {}
    expected = {str(i) + state + '.' + kind for i in range(54)
                for state in ['', '-edit', '-projection'] for kind in ['rgba', 'depth']}
    for name in names:
        actual = {p.name for p in (root / name).iterdir() if p.suffix in ['.rgba', '.depth']}
        if actual != expected:
            raise RuntimeError(f'{name}: missing={sorted(expected-actual)}, extra={sorted(actual-expected)}')
    for path in sorted(expected):
        payloads = [(root / name / path).read_bytes() for name in names]
        if any(len(payload) != 96*96*4 for payload in payloads):
            raise RuntimeError(f'Invalid payload length: {path}')
        if path.endswith('.depth'):
            for payload in payloads:
                depth = struct.unpack('<' + 'f' * (96*96), payload)
                if not all(math.isfinite(v) and 0 <= v <= 1 for v in depth) or not any(v < 1 for v in depth):
                    raise RuntimeError(f'Invalid or empty depth coverage: {path}')
        if any(payload != payloads[0] for payload in payloads[1:]):
            raise RuntimeError(f'Exact comparison failed: {path}')
        hashes[path] = hashlib.sha256(payloads[0]).hexdigest()
        count += 1
    for name in names:
        for i in range(54):
            initial = (root / name / (str(i) + '.rgba')).read_bytes()
            edited = (root / name / (str(i) + '-edit.rgba')).read_bytes()
            projected = (root / name / (str(i) + '-projection.rgba')).read_bytes()
            if initial == edited or edited == projected:
                raise RuntimeError(f'{name}/{i}: material or projection edit had no visible effect')
    return {'status': 'PASS', 'controls': names, 'scene_configurations': 54,
            'states_per_scene': ['initial', 'material edit', 'projection edit'],
            'compared_buffers': count, 'comparison': 'byte-identical RGBA8 and float depth',
            'timing_qualification': False, 'sha256': hashes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current', type=Path)
    parser.add_argument('--retained', type=Path)
    parser.add_argument('--reference', type=Path, default=os.environ.get('COIN_PBR_REFERENCE_EXECUTABLE'), help='Optional executable compiled from the pinned PBR branch')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compare-existing', nargs='+', help='Only compare previously generated output subdirectories')
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if args.compare_existing:
        names = args.compare_existing
    else:
        if not args.current or not args.retained:
            parser.error('--current and --retained are required to generate captures')
        names = []
        # Each process completes before the next starts. Never overlap GPU controls.
        for name, executable in [('control', args.current), ('retained', args.retained), ('reference', args.reference)]:
            if executable is None:
                continue
            destination = root / name
            destination.mkdir(exist_ok=True)
            with (root / (name + '.log')).open('w') as log:
                subprocess.run([str(executable.resolve()), str(destination)],
                               stdout=log, stderr=subprocess.STDOUT, timeout=180, check=True)
            names.append(name)
    if len(names) < 2:
        parser.error('At least two controls are required')
    report = compare(root, names)
    (root / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"PBR comparison PASS: {report['scene_configurations']} scenes, 3 states, {report['compared_buffers']} exact buffers, {len(names)} paths")


if __name__ == '__main__':
    main()
