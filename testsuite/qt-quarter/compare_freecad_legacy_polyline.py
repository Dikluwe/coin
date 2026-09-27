#!/usr/bin/env python3
"""Compare the real legacy Polyline contribution against Coin/GL (DPI 1x)."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image


def compare(oracle, candidate):
    def image(directory, stage):
        return np.asarray(Image.open(directory / (stage + '.png')).convert('RGB'),
                          dtype=np.int16)
    a0 = image(oracle, 'polyline-off')
    b0 = image(candidate, 'polyline-off')
    if a0.shape != b0.shape:
        raise ValueError('viewport dimensions differ')
    metrics = {}
    for stage in ('polyline-first', 'polyline-updated', 'polyline-cancelled'):
        a = image(oracle, stage)
        b = image(candidate, stage)
        if a.shape != a0.shape or b.shape != b0.shape:
            raise ValueError('viewport dimensions changed')
        difference = (a-a0)[45:245, 30:200] - (b-b0)[45:245, 30:200]
        ad = (a-a0)[45:245, 30:200]
        bd = (b-b0)[45:245, 30:200]
        affected = np.any((np.abs(ad) > 10) | (np.abs(bd) > 10), axis=2)
        count = int(affected.sum())
        roi_mae = float(np.abs(difference).mean())
        affected_mae = float(np.abs(difference)[affected].mean()) if count else 0.0
        metrics[stage] = dict(roi_mae=roi_mae, affected_mae=affected_mae,
                              affected_pixels=count)
        # The broad ROI alone would hide a missing thin line. Check its support
        # separately, allowing edge rasterization differences between GL/BGFX.
        if (roi_mae > 6 or affected_mae > 16 or
                (stage != 'polyline-cancelled' and count < 100)):
            raise ValueError('Polyline contribution differs: ' + json.dumps(metrics))
    return metrics


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(compare(args.oracle, args.candidate), indent=2))
