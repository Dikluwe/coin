from pathlib import Path
import argparse, hashlib, json, os, subprocess
from PIL import Image, ImageChops

parser = argparse.ArgumentParser()
parser.add_argument('--viewer', default='/tmp/coin-pbr-bgfx/bin/coin_render_viewer')
parser.add_argument('--output', default='/tmp/coin-pbr-m0/matrix')
parser.add_argument('--cad-source')
args = parser.parse_args()
out = Path(args.output); out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env.update(DISPLAY=':0', VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',
           COIN_BGFX_RENDERER='vulkan')
rows = []
presets = ['iso-perspective', 'iso-orthographic', 'front', 'top', 'right', 'detail']

def run(name, options):
    prefix = out / name
    command = [args.viewer, *options, '--frames', '2', '--capture-prefix', str(prefix)]
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
    prefix.with_suffix('.log').write_text(result.stdout + result.stderr)
    row = {'case': name, 'command': command, 'exit_code': result.returncode}
    if result.returncode == 0:
        images = [Image.open(f'{prefix}-{i:03d}.ppm').convert('RGB') for i in range(2)]
        metadata = json.loads(Path(f'{prefix}-000.json').read_text())
        row.update(adapter=metadata['adapter'], renderer=metadata['renderer'],
                   stable_pixels=ImageChops.difference(*images).getbbox() is None,
                   pixels_sha256=hashlib.sha256(images[0].tobytes()).hexdigest(),
                   dimensions=images[0].size)
        for i, im in enumerate(images):
            im.save(f'{prefix}-{i:03d}.png')
        # This is the legacy baseline only. PBR needs geometric/depth masks,
        # since dark metals cannot be segmented by background color reliably.
        mask = ImageChops.difference(images[0], Image.new('RGB', images[0].size,
            images[0].getpixel((0, 0)))).convert('L').point(lambda x: 255 if x else 0)
        row['legacy_foreground_coverage'] = mask.histogram()[255] / (images[0].width * images[0].height)
        row['nonconstant'] = any(lo != hi for lo, hi in images[0].getextrema())
        row['pass'] = (row['stable_pixels'] and metadata['vendor_id'] == 0x1002 and
            metadata['renderer'] == 1 and row['nonconstant'] and row['legacy_foreground_coverage'] > 0.01)
    else:
        row['pass'] = False
    rows.append(row)
    (out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(name, 'PASS' if row['pass'] else 'FAIL', flush=True)
    return row

for fixture in ['flange', 'assembly', 'housing']:
    for preset in presets:
        run(f'{fixture}-{preset}-offscreen', ['--fixture', fixture, '--view', preset, '--offscreen'])
    window = run(f'{fixture}-iso-perspective-window', ['--fixture', fixture, '--view', 'iso-perspective'])
    reference = next(r for r in rows if r['case'] == f'{fixture}-iso-perspective-offscreen')
    window['window_offscreen_equal'] = window.get('pixels_sha256') == reference.get('pixels_sha256')
    window['pass'] = window['pass'] and window['window_offscreen_equal']
run('housing-cut', ['--fixture', 'housing', '--view', 'iso-perspective', '--offscreen', '--cut'])
if args.cad_source:
    source = Path(args.cad_source)
    run('freecad-assembly', ['--scene', str(source), '--view', 'iso-perspective', '--offscreen'])
    (out / 'cad-source.json').write_text(json.dumps({
        'path': str(source), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'role': 'Existing real FreeCAD mesh export; source is not modified.'
    }, indent=2) + '\n')
for invalid in [['--fixture', 'unknown'], ['--view', 'unknown'], ['--width', '0']]:
    result = subprocess.run([args.viewer, '--offscreen', *invalid], env=env, capture_output=True, text=True, timeout=60)
    rows.append({'case': 'reject-' + '-'.join(invalid), 'exit_code': result.returncode, 'pass': result.returncode == 2})
(out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
print('TOTAL', sum(r['pass'] for r in rows), '/', len(rows), flush=True)
raise SystemExit(0 if all(r['pass'] for r in rows) else 1)
