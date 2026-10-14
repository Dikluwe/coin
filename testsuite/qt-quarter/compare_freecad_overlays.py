#!/usr/bin/env python3
"""Compare real viewer overlay captures against a fresh Coin/GL oracle."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def compare(oracle, candidate, scale):
    results = {}
    for name in ('axes-on', 'axes-rotated'):
        reference = np.asarray(Image.open(oracle / (name + '.png')).convert('RGB'), dtype=float)
        actual = np.asarray(Image.open(candidate / (name + '.png')).convert('RGB'), dtype=float)
        if reference.shape != actual.shape:
            raise AssertionError('different viewport dimensions')
        side = 100 * scale
        mae = float(np.abs(reference[-side:, -side:] - actual[-side:, -side:]).mean())
        if mae > 6:
            raise AssertionError(name + ' corner RGB MAE exceeds 6: ' + str(mae))
        results[name + '_mae'] = mae

    # Compare the overlay contribution, independent of background rendering.
    contribution = []
    for directory in (oracle, candidate):
        before = np.asarray(Image.open(directory / 'rubber-off.png').convert('RGB'), dtype=float)
        during = np.asarray(Image.open(directory / 'rubber-on.png').convert('RGB'), dtype=float)
        contribution.append(during - before)
    difference = np.abs(contribution[0] - contribution[1])
    mae = float(difference[100*scale:220*scale, 70*scale:230*scale].mean())
    if mae > 6:
        raise AssertionError('rubber-band contribution RGB MAE exceeds 6: ' + str(mae))
    results['rubber_contribution_mae'] = mae
    borders = {'top': (78, 83, 60, 240), 'bottom': (238, 243, 60, 240),
               'left': (90, 230, 48, 53), 'right': (90, 230, 248, 253)}
    for name, (y0, y1, x0, x1) in borders.items():
        mae = float(difference[y0*scale:y1*scale, x0*scale:x1*scale].mean())
        if mae > 6:
            raise AssertionError(name + ' rubber-band border RGB MAE exceeds 6: ' + str(mae))
        results[name + '_border_mae'] = mae
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--scale', type=int, choices=(1, 2), default=1)
    args = parser.parse_args()
    print(json.dumps(compare(args.oracle, args.candidate, args.scale), indent=2))
