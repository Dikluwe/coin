#!/usr/bin/env python3
"""Validate paired FreeCAD captures, excluding the navigation cube."""
import json
import sys
from pathlib import Path
from PIL import Image

base = Path(sys.argv[1])
checks = []

def check(name, condition):
    checks.append({'check': name, 'passed': bool(condition)})

for variant in ('master', 'fixed'):
    folder = base / ('freecad-' + variant + '-vp')
    result = json.loads((folder / 'result.json').read_text())
    fixed = variant == 'fixed'
    check(variant + ': macro completed', result.get('ok') is True)
    check(variant + ': one Coin library loaded', len(result['coin_mapped']) == 1)
    expected_lib = '/tmp/coin-issue417-branch-build/' if fixed else '/tmp/coin-issue417-study/master-build/'
    check(variant + ': expected Coin loaded', result['coin_mapped'][0].startswith(expected_lib))
    for case in result['cases']:
        name = variant + ': ' + case['name']
        manual = case['name'] == 'manual-control'
        check(name + ': initial bounds', case['bbox_before_empty'] == (not fixed))
        check(name + ': resulting bounds', case['bbox_after_empty'] == (not fixed and not manual))
        check(name + ': camera changed', (case['camera'] != [0.0, 0.0, 1.0]) == (fixed or manual))
        image = Image.open(folder / (case['name'] + '-viewer.png')).convert('RGB')
        check(name + ': image dimensions', image.size == (640, 480))
        # Test scenes contain only white text here. The navigation cube ends above y=150.
        region = image.crop((0, 150, 640, 480))
        pixels = sum(min(pixel) > 180 for pixel in region.getdata())
        check(name + ': text rendered', (pixels > 0) == (fixed or manual))
        print(name, 'bright_text_pixels=', pixels)
    check(variant + ': Screen annotation mode', result['annotation']['display_mode'] == 'Screen')
    check(variant + ': box volume', abs(result['shape_volume'] - 1000) < 1e-6)
    for filename in ('annotation-screen.png', 'part-and-annotation.png'):
        image = Image.open(folder / filename).convert('RGB').crop((0, 150, 640, 480))
        check(variant + ': ' + filename + ' nonempty', any(max(pixel) > 0 for pixel in image.getdata()))
failures = sum(not item['passed'] for item in checks)
summary = {'checks': checks, 'total': len(checks), 'failures': failures}
(base / 'freecad-viewprovider-verification.json').write_text(json.dumps(summary, indent=2))
print(f'{len(checks)} FreeCAD checks, {failures} failures')
for item in checks:
    if not item['passed']:
        print('FAIL:', item['check'])
sys.exit(1 if failures else 0)
