#!/usr/bin/env python3
"""Audit the local retained contribution map; does not export or apply patches."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / 'cmake/patches/freecad-retained-contributions.json'


def git_output(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def changed_paths(base_commit):
    tracked = git_output('diff', '--name-only', '-z', base_commit, '--')
    untracked = git_output('ls-files', '--others', '--exclude-standard', '-z')
    return sorted({p for p in (tracked + untracked).decode().split('\0')
                   if p and not p.startswith('docs/')})


def native_dependency_errors(path):
    """A narrow dependency gate, not proof of complete backend independence."""
    source = (ROOT / path).read_text(encoding='utf-8')
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    source = re.sub(r'//[^\n]*', '', source)
    errors = []
    for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', source, re.M):
        include = match.group(1)
        if (re.search(r'(^|/)(coinrender|coinbgfx|coinwgpu)(/|$)', include)
                or Path(include).name.startswith(('CoinRender', 'CoinWgpu', 'CoinBgfx'))):
            errors.append(f'{path}: native candidate includes executor/integration header {include}')
    token = re.search(r'\b(?:CoinRender\w*|CoinWgpu\w*|CoinBgfx\w*|COIN_RENDER_\w*|COIN_WGPU_\w*|COIN_BGFX_\w*)\b|\b(?:wgpu|bgfx)::', source)
    if token:
        errors.append(f'{path}: native candidate contains integration/executor token {token.group(0)}')
    return errors


def upstream_relation(git_dir, revision, path):
    result = subprocess.run(['git', '--git-dir=' + str(git_dir), 'show', revision + ':' + path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        return {'relation': 'not_in_pinned_upstream'}
    same = result.stdout == (ROOT / path).read_bytes()
    return {'relation': 'same_bytes' if same else 'different_bytes',
            'sha256': hashlib.sha256(result.stdout).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument('--upstream-git-dir', type=Path,
                        help='Optional existing local Git repository/bare clone; no fetch is performed.')
    parser.add_argument('--output', type=Path, help='Save the full JSON audit report.')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    errors = []
    records = manifest['files']
    by_path = {row['path']: row for row in records}
    if len(by_path) != len(records):
        errors.append('Duplicate paths in contribution manifest')
    feature_ids = {feature['id'] for feature in manifest['features']}
    valid_paths = set()
    for row in records:
        relative = Path(row['path'])
        if relative.is_absolute() or '..' in relative.parts:
            errors.append('Manifest path must stay inside the repository: ' + row['path'])
            continue
        valid_paths.add(row['path'])
        if not (ROOT / relative).is_file():
            errors.append('Manifest file is missing: ' + row['path'])
        for feature in row['features']:
            if feature not in feature_ids:
                errors.append(row['path'] + ': unknown feature ' + feature)
    changed = changed_paths(manifest['base_commit'])
    unclassified = sorted(set(changed) - by_path.keys())
    errors.extend('Changed file needs classification: ' + path for path in unclassified)
    for path in manifest['portable_boundary_files']:
        if path not in by_path:
            errors.append('Boundary path is not classified: ' + path)
        elif path in valid_paths and (ROOT / path).is_file():
            errors.extend(native_dependency_errors(path))
    upstream_valid = False
    if args.upstream_git_dir:
        reference = subprocess.run(
            ['git', '--git-dir=' + str(args.upstream_git_dir), 'cat-file', '-e',
             manifest['upstream']['revision'] + '^{commit}'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        upstream_valid = reference.returncode == 0
        if not upstream_valid:
            errors.append('Pinned upstream commit is unavailable in --upstream-git-dir')
    audit_files = []
    for row in records:
        path = ROOT / row['path']
        if row['path'] not in valid_paths or not path.is_file():
            continue
        entry = dict(row, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        if upstream_valid:
            entry['upstream'] = upstream_relation(args.upstream_git_dir,
                                                  manifest['upstream']['revision'], row['path'])
        audit_files.append(entry)
    report = {'schema_version': 1,
              'branch': git_output('branch', '--show-current').decode().strip(),
              'base_commit': manifest['base_commit'],
              'upstream': manifest['upstream'],
              'scope': 'Changed non-documentation files versus the integration base, plus non-ignored new files.',
              'candidate_readiness': manifest['policy']['readiness'],
              'classified_files': len(records),
              'changed_files': len(changed),
              'bucket_counts': dict(sorted(Counter(row['bucket'] for row in records).items())),
              'native_boundary_files': len(manifest['portable_boundary_files']),
              'unclassified': unclassified,
              'errors': errors,
              'files': audit_files}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Retained contribution boundaries: ' + ('FAIL' if errors else 'PASS'))
    print(f"Classified: {len(records)}; changed in scope: {len(changed)}; native boundary: {len(manifest['portable_boundary_files'])}")
    for bucket, count in report['bucket_counts'].items():
        print(f'  {bucket}: {count}')
    for error in errors:
        print('ERROR: ' + error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
