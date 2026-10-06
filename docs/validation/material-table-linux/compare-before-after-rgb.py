#!/usr/bin/env python3
"""Recompute same-renderer RGB pairs; no build, benchmark or GPU commands."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
sys.dont_write_bytecode = True

def require(condition, message):
    if not condition:
        raise ValueError(message)

def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def module(path):
    spec = importlib.util.spec_from_file_location('material_rgb_helper', path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper

def analyze(prefix, summary_helper, image_analyzer, pixels=True):
    summary = module(summary_helper)
    results = []
    counts = {}
    for label, expected in [('original', 3), ('large', 4)]:
        datasets = {}
        for role in ('before', 'after'):
            directory = Path(str(prefix) + '-rgb-' + role + '-' + label)
            rows = json.loads((directory/'results.json').read_text())
            manifest = json.loads((directory/'manifest.json').read_text())
            require(len(rows) == expected, 'RGB process cardinality differs')
            require(len({(r['case'], r['variant'], r['round']) for r in rows}) == expected,
                    'Duplicate RGB identity')
            require(all(r['variant'] == 'wgpu-vulkan' for r in rows), 'Unexpected RGB variant')
            counts[label + '-' + role] = len(rows)
            datasets[role] = dict(original_directory=str(directory), processes=rows, manifest=manifest)
        result = summary.compare_rgb(SimpleNamespace(require=require, sha256=sha256),
                                     image_analyzer, datasets['before'], datasets['after'])
        require(not result['before_only'] and not result['after_only'], 'RGB pairing incomplete')
        require(result['comparison_count'] == expected*7, 'RGB frame count differs')
        require(result['all_rgb_identical'], 'RGB images differ')
        for item in result['results']:
            item['scene_label'] = label
            require(item['logical_frame'] in range(0, 601, 100), 'Unexpected logical frame')
            require(item['before']['rgb_fnv64'] == item['after']['rgb_fnv64'] and
                    item['before']['rgba_fnv64'] == item['after']['rgba_fnv64'],
                    'Captured image digest differs')
        results.extend(result['results'])
    require(len(results) == 49, 'Require 49 RGB pairs')
    for label in ('original', 'large'):
        for case in {r['case'] for r in results if r['scene_label'] == label}:
            group = [r for r in results if r['scene_label'] == label and r['case'] == case]
            changed = len({r['before']['rgb_fnv64'] for r in group}) > 1
            states_changed = len({r['state_fnv64'] for r in group}) > 1
            require(changed == (case != 'static') and states_changed == (case != 'static'),
                    'Motion/static evidence differs: ' + label + '/' + case)
    return {'processes': 14, 'produced_ppm_files': 98, 'pairs': 49,
            'all_rgb_identical': True, 'rgb_pixels_recomputed': True,
            'process_counts': counts, 'results': results,
            'helper_sha256': sha256(summary_helper), 'image_analyzer_sha256': sha256(image_analyzer),
            'comparison': 'Same wgpu variant/case/logical frame; same scene-state digest, byte exact RGB',
            'compiled_source_revisions': {'before': '96e5ed80fa3ab3c9016cf10e6bbfc9b638335a33',
                                         'after': 'a0bda8b5ccc8d04efa96c5c807b88e5aa4e34c9d'}}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path('/tmp/coin-render-material-table'))
    parser.add_argument('--summary-helper', type=Path, default=Path('/tmp/coin-render-wgpu-motion-summary.py'))
    parser.add_argument('--image-analyzer', type=Path, default=Path('/tmp/coin-render-first-frame/scripts/coinrender/analyze_animation_images.py'))
    parser.add_argument('--output', type=Path, default=Path('/tmp/coin-render-material-table-rgb.json'))
    args = parser.parse_args()
    require(not args.output.exists(), 'Preserve previous RGB result')
    data = analyze(args.prefix, args.summary_helper, args.image_analyzer)
    args.output.write_text(json.dumps(data, indent=2, allow_nan=False)+'\n')
    print('49 RGB pairs byte exact; 14 processes, 98 PPM files; motion/static and state digests checked')

if __name__ == '__main__':
    main()
