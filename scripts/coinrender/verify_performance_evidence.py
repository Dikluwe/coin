#!/usr/bin/env python3
"""Verify archived performance evidence; never runs builds or GPU commands."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--source-tree', type=Path,
                        help='Optionally check the four measured source inputs against this checkout')
    parser.add_argument('--check-local-binaries', action='store_true',
                        help='Check frozen files at the original local paths recorded in the manifests')
    args = parser.parse_args()
    root = args.directory.resolve()
    expected = json.loads((root / 'evidence-sha256.json').read_text())
    for relative, digest in expected.items():
        path = root / relative
        assert path.resolve().is_relative_to(root), relative
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, relative
    for name in ('baseline-binaries.json', 'final-binaries.json'):
        manifest = json.loads((root / name).read_text())
        assert hashlib.sha256((root / 'city.iv').read_bytes()).hexdigest() == manifest['scene_sha256']
        for filename, digest in manifest.get('source_sha256', {}).items():
            assert hashlib.sha256((root / 'measured-sources' / filename).read_bytes()).hexdigest() == digest, filename
        if args.check_local_binaries:
            for filename, digest in manifest['files'].items():
                assert hashlib.sha256(Path(filename).read_bytes()).hexdigest() == digest, filename
        if args.source_tree:
            for filename, digest in manifest.get('source_sha256', {}).items():
                assert hashlib.sha256((args.source_tree / filename).read_bytes()).hexdigest() == digest, filename
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('archived_analysis', root / 'coin-perf-analyze.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    actual = module.analyze(root / 'measure')
    saved = json.loads((root / 'coin-perf-analysis.json').read_text())
    assert actual == saved, 'statistics differ from buffered CSV'
    visual = json.loads((root / 'visual/exact-ab.json').read_text())
    assert len(visual) == 126 and all(row['exact_ppm'] for row in visual)
    assert len({(row['case'], row['api'], row['frame']) for row in visual}) == 126
    window = json.loads((root / 'window-visual/exact-ab.json').read_text())
    assert len(window) == 36 and all(row['same_rgba_digest'] for row in window)
    assert len({(row['case'], row['api'], row['logical_frame']) for row in window}) == 36
    print(f"Verified {len(expected)} file hashes; {actual['processes']} processes; "
          f"{actual['measured_frames']} measured frames; 126 exact PPM results; 36 window digest pairs")
    print('Binary hashes and image hashes are archived observations; this check does not rerun GPU validation.')


if __name__ == '__main__':
    main()
