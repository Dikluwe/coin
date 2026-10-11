#!/usr/bin/env python3
"""Run the centered QuadMesh CoinGL oracle in fresh GPU processes."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--multirow', action='store_true')
parser.add_argument('--phong-only', action='store_true')
args = parser.parse_args()

rows = []
for material in (2, 3, 5):  # OVERALL, PER_PART, PER_FACE
    for normal in (2, 3, 5, 7):  # OVERALL, PER_PART, PER_FACE, PER_VERTEX
        for texture in (0, 1):
            for phong in ((1,) if args.phong_only else (0, 1)):
                command = [str(args.exe), '--probe-precise-positive',
                           str(material), str(normal), str(texture), str(phong)]
                if args.multirow:
                    command.append('1')
                process = subprocess.run(command, env=os.environ.copy(),
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True,
                                         check=False)
                row = {'material': material, 'normal': normal,
                       'texture': bool(texture), 'phong': bool(phong),
                       'multirow': args.multirow, 'exit': process.returncode,
                       'maxima': [int(value) for value in
                                  re.findall(r'max=(\d+)', process.stdout)],
                       'output': process.stdout.strip()}
                rows.append(row)
                if process.returncode:
                    print('FAIL', material, normal, texture, phong,
                          process.stdout.strip(), flush=True)

args.out.write_text(json.dumps(rows, indent=2) + '\n')
passed = sum(row['exit'] == 0 for row in rows)
maximum = max((max(row['maxima']) for row in rows if row['maxima']), default=-1)
print(f'precise QuadMesh {passed}/{len(rows)} PASS; max RGB={maximum}')
raise SystemExit(0 if passed == len(rows) else 1)
