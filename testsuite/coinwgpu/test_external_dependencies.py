#!/usr/bin/env python3
"""Check resolved external crate pins and preserved patches without a GPU.

Run after `cargo fetch --locked`; metadata resolution is deliberately offline.
No dependency sources are modified by the reverse-apply checks.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from urllib.parse import parse_qs, urlsplit

PACKAGES = {
    'naga': '24.0.0',
    'wgpu-hal': '24.0.4',
    'wgpu-core': '24.0.5',
    'glow': '0.16.0',
}
OPTIONS = None


def run(command, **kwargs):
    result = subprocess.run([str(arg) for arg in command], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            **kwargs)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


class ExternalDependencies(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        manifest = OPTIONS.source_root / 'src/rendering/coinwgpu/rust_bridge/Cargo.toml'
        with tempfile.TemporaryDirectory(prefix='coin-external-metadata-') as tmp:
            env = os.environ.copy()
            env.update(CARGO_TARGET_DIR=str(Path(tmp) / 'target'), TMPDIR=tmp)
            data = json.loads(run([
                OPTIONS.cargo, 'metadata', '--offline', '--locked',
                '--format-version', '1', '--manifest-path', manifest], env=env))
        cls.packages = {name: [p for p in data['packages'] if p['name'] == name]
                        for name in PACKAGES}

    def test_resolved_sources_are_pinned_external_revisions(self):
        revisions = set()
        for name, version in PACKAGES.items():
            with self.subTest(package=name):
                self.assertEqual(len(self.packages[name]), 1)
                package = self.packages[name][0]
                self.assertEqual(package['version'], version)
                source = package['source'] or ''
                self.assertTrue(source.startswith('git+https://'), source)
                url = urlsplit(source[4:])
                self.assertEqual(url.netloc, 'github.com')
                self.assertEqual(re.sub(r'\.git$', '', url.path.rstrip('/')),
                                 '/Dikluwe/coinrender-rust-deps')
                query = parse_qs(url.query)
                self.assertEqual(set(query), {'rev'})
                revision = query['rev'][0]
                self.assertRegex(revision, r'^[0-9a-f]{40}$')
                self.assertEqual(url.fragment, revision)
                revisions.add(revision)
        self.assertEqual(len(revisions), 1, 'the paired fixes must use one source revision')

    def test_compact_patches_match_the_resolved_dependency_sources(self):
        for name, version in PACKAGES.items():
            with self.subTest(package=name):
                self.assertEqual(len(self.packages[name]), 1)
                package = self.packages[name][0]
                patch = (OPTIONS.source_root / 'cmake/patches/rust-dependencies' /
                         (name + '-' + version + '.patch'))
                self.assertIn(b'diff --git ', patch.read_bytes())
                run(['git', 'apply', '--reverse', '--check', patch],
                    cwd=Path(package['manifest_path']).parent)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--cargo', default='cargo')
    OPTIONS = parser.parse_args()
    OPTIONS.source_root = OPTIONS.source_root.resolve()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ExternalDependencies)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() and not result.skipped else 1


if __name__ == '__main__':
    raise SystemExit(main())
