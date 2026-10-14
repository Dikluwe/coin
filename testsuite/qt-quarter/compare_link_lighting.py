#!/usr/bin/env python3
"""Gate planar-link lighting against matched Coin/GL captures, not all shading."""
import argparse
import json
from pathlib import Path


def require_lighting_parity(measurements, maximum=1.0):
    baselines = [item for item in measurements if item['stage'].endswith('baseline')]
    if len(measurements) != 88 or len(baselines) != 16:
        raise RuntimeError('incomplete planar-link comparison matrix')
    if any(not 0 <= item['rgb_mae_foreground'] <= maximum for item in baselines):
        raise RuntimeError('planar-link lighting differs from Coin/GL')


def main():
    import numpy as np
    from PySide6.QtGui import QImage
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    native = json.loads((args.native / 'results.json').read_text())
    references = json.loads((args.reference / 'results.json').read_text())
    cells = {(r, m, s) for r in ('vulkan', 'opengl')
             for m in ('object', 'weighted_oit') for s in (1, 2)}
    if len(native) != 8 or {(c['renderer'], c['mode'], c['scale']) for c in native} != cells:
        raise RuntimeError('incomplete BGFX matrix')
    if any(c['status'] != 'PASS' or c.get('test') != 'freecad-mouse-links' for c in native):
        raise RuntimeError('native link matrix did not pass')
    if len(references) != 2 or {c['scale'] for c in references} != {1, 2} or any(
            c['status'] != 'REFERENCE_PASS' or c.get('test') != 'freecad-mouse-links'
            for c in references):
        raise RuntimeError('Coin/GL reference did not pass')
    provenance = [json.loads((p / 'provenance.json').read_text())
                  for p in (args.native, args.reference)]
    if not provenance[0]['harness_sha256'] or (
            provenance[0]['harness_sha256'] != provenance[1]['harness_sha256']):
        raise RuntimeError('harness builds differ')

    def pixels(path):
        image = QImage(str(path)).convertToFormat(QImage.Format_RGB888)
        if image.isNull():
            raise RuntimeError('missing capture: ' + str(path))
        rows = np.frombuffer(image.constBits(), dtype=np.uint8).reshape(
            image.height(), image.bytesPerLine())
        return rows[:, :image.width()*3].reshape(image.height(), image.width(), 3).astype(np.int16)

    stages = ('links-baseline', 'links-a-hover', 'links-a-selected', 'links-b-hover',
              'links-both-selected', 'links-only-b', 'links-only-a', 'links-cleared',
              'links-resized-baseline', 'links-resized-hover', 'links-resized-cleared')
    references = {c['scale']: Path(c['artifacts']) for c in references}
    measurements = []
    for cell in native:
        actual, expected = Path(cell['artifacts']), references[cell['scale']]
        for stage in stages:
            baseline = 'links-resized-baseline' if 'resized' in stage else 'links-baseline'
            a, b = pixels(actual / (stage+'.png')), pixels(expected / (stage+'.png'))
            ab, bb = pixels(actual / (baseline+'.png')), pixels(expected / (baseline+'.png'))
            if a.shape != b.shape or a.shape != ab.shape or b.shape != bb.shape:
                raise RuntimeError('capture geometry differs')
            foreground = (np.max(np.abs(ab-ab[0, 0]), axis=2) > 8) | (
                np.max(np.abs(bb-bb[0, 0]), axis=2) > 8)
            if not np.any(foreground):
                raise RuntimeError('empty foreground cannot pass')
            am, bm = np.max(np.abs(a-ab), axis=2) > 8, np.max(np.abs(b-bb), axis=2) > 8
            union = int(np.count_nonzero(am | bm))
            measurements.append(dict(renderer=cell['renderer'], mode=cell['mode'],
                scale=cell['scale'], stage=stage,
                rgb_mae_foreground=float(np.mean(np.abs(a-b)[foreground])),
                highlight_mask_iou=int(np.count_nonzero(am & bm))/union if union else 1.0))
    require_lighting_parity(measurements)
    args.output.write_text(json.dumps(dict(status='PASS', scope='planar-link-lighting',
        maximum_baseline_rgb_mae=1.0, mask_threshold_rgb=8, comparisons=measurements), indent=2))
    print('PASS: planar-link lighting; 88 matched comparisons')


if __name__ == '__main__':
    main()
