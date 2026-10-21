#!/usr/bin/env python3
"""Verify a two-GPU P20 campaign and retain native CoinGL failures separately."""
import argparse
import importlib.util
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign', type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('p20', Path(__file__).with_name('run_p20_physical_matrix.py'))
    p20 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(p20)
    root = args.campaign
    summary = {'canonical_cells': [], 'native_controls': [], 'cross_gpu': [],
               'unavailable': [{'device': 'intel', 'cells_per_policy': 16,
                                'reason': 'no physical Intel GPU on this host'}], 'failures': []}
    for device in p20.DEVICES:
        for policy in ('native', 'canonical'):
            result = json.loads((root / f'{device}-{policy}' / 'results.json').read_text())
            if len(result['runs']) != 16:
                summary['failures'].append(f'{device}/{policy}: incomplete 16-cell campaign')
            if policy == 'canonical':
                summary['failures'].extend(result['failures'])
                cpu = [c for c in result['comparisons'] if c['backends'][0] == 'canonical-CPU']
                if len(cpu) != 12 or not all(c['passed'] for c in cpu):
                    summary['failures'].append(f'{device}: incomplete CPU qualification')
                for cell in result['runs']:
                    if cell['backend'] != 'coin-gl':
                        if not cell['api_verified'] or not cell['physical_gpu_verified'] or cell['rasterization_policy'] != 'canonical':
                            summary['failures'].append(f"unverified portable cell {cell['key']}")
                        summary['canonical_cells'].append(dict(cell, device=device, qualification='pass: CPU RGB and API gates'))
            else:
                expected = {f'amd-offscreen-transparent-overlap-{backend}-CoinGL' for backend in ('bgfx-vulkan', 'wgpu-vulkan')} if device == 'amd' else set()
                actual = {failure['key'] for failure in result['failures']}
                if actual != expected:
                    summary['failures'].append(f'{device}: unexpected native failures {actual ^ expected}')
                comparisons = [c for c in result['comparisons'] if 'passed' in c]
                if len(comparisons) != 12:
                    summary['failures'].append(f'{device}: incomplete native comparisons')
                summary['native_controls'].append({'device': device, 'executed': len(result['runs']),
                    'passed_rgb': sum(c['passed'] for c in comparisons), 'total_rgb': len(comparisons),
                    'failures': result['failures']})
    for target in ('window', 'offscreen'):
        for scene in p20.SCENES:
            for backend in p20.BACKENDS[1:]:
                metrics = p20.rgb_metrics(root/'amd-canonical'/f'amd-{target}-{scene}-{backend}.ppm',
                                          root/'nvidia-canonical'/f'nvidia-{target}-{scene}-{backend}.ppm')
                passed = metrics['max'] <= 2 and metrics['mae'] <= 1.0
                summary['cross_gpu'].append({'target': target, 'scene': scene, 'backend': backend,
                    'policy': 'RGB max<=2 MAE<=1.0', 'metrics': metrics, 'passed': passed})
                if not passed:
                    summary['failures'].append(f'cross-GPU {target}/{scene}/{backend}')
    if len(summary['canonical_cells']) != 24:
        summary['failures'].append('incomplete portable cells')
    (root/'portable-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(f"canonical={len(summary['canonical_cells'])}/24 cross_gpu={sum(c['passed'] for c in summary['cross_gpu'])}/12 failures={len(summary['failures'])}")
    return bool(summary['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
