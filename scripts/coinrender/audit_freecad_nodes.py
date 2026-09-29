#!/usr/bin/env python3
"""Discover FreeCAD GL node candidates and special Coin creations; no support inference.

Lexical discovery (not a C++ parser). Scope: src/Gui and src/Mod, excluding
src/3rdParty. Every discovered GL class needs an explicit review.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
METHOD = re.compile(r'\b(\w+)::(GLRender(?:BelowPath|InPath|OffPath)?|callback|generatePrimitives|doAction)\s*\([^)]*\)\s*\{')
SPECIAL = ('SoImage', 'SoText2', 'SoTexture3', 'SoTextureCubeMap', 'SoSceneTextureCubeMap', 'SoShadowGroup',
           'SoShaderProgram', 'SoVertexShader', 'SoFragmentShader', 'SoGeometryShader')
CREATE = re.compile(r'\bnew\s+('+'|'.join(SPECIAL)+r')\b|\bcoin\.('+'|'.join(SPECIAL)+r')\s*\(')
GPU = re.compile(r'\b(glCreateShader|glUseProgram|QOpenGLShaderProgram|QOpenGLWidget)\b')

def mask(text):
    return TOKENS.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text)

def revision(root):
    return subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()

def audit(root, review):
    nodes, special, external, scanned = {}, [], [], 0
    digest = hashlib.sha256()
    for area in ('Gui', 'Mod'):
        for path in sorted((root/'src'/area).rglob('*')):
            if path.suffix not in ('.cpp', '.cxx', '.h', '.hxx', '.py') or not path.is_file():
                continue
            relative = path.relative_to(root).as_posix()
            raw = path.read_bytes()
            text = raw.decode('utf-8', errors='replace')
            # Python comments also need exclusion; strings/comments are never evidence of creation.
            clean = mask(text)
            if path.suffix == '.py':
                clean = re.sub(r'#[^\n]*', lambda m: ' '*len(m[0]), clean)
            digest.update(relative.encode()+b'\0'+raw+b'\0')
            scanned += 1
            if path.suffix in ('.cpp', '.cxx', '.h', '.hxx'):
                for found in METHOD.finditer(clean):
                    name, method = found[1], found[2]
                    item = nodes.setdefault(name, {'methods': []})
                    start, depth, end = found.end(), 1, found.end()
                    while end < len(clean) and depth:
                        depth += (clean[end] == '{') - (clean[end] == '}')
                        end += 1
                    body = clean[start:end-1]
                    item['methods'].append({'method': method, 'source': relative,
                        'line': clean.count('\n', 0, found.start())+1,
                        'empty_body': not body.strip(),
                        'sha256': hashlib.sha256(raw).hexdigest(),
                        'action_types': sorted(set(re.findall(r'\b(?:SoWgpuRenderAction|SoBgfxRenderAction|CoinRenderAction)\b', body)))})
            for found in CREATE.finditer(clean):
                special.append({'node': found[1] or found[2], 'source': relative,
                                'line': clean.count('\n', 0, found.start())+1})
            hits = list(GPU.finditer(clean))
            if hits:
                external.append({'source': relative, 'mechanisms': sorted(set(m[1] for m in hits)),
                                 'first_line': clean.count('\n',0,hits[0].start())+1})
    gl_nodes = {name: value for name, value in sorted(nodes.items())
                if any(m['method'].startswith('GLRender') for m in value['methods'])}
    for name, value in gl_nodes.items():
        value['review'] = review.get(name, {'status': 'UNREVIEWED'})
    return {'schema': 1, 'freecad_revision': revision(root), 'source_digest': digest.hexdigest(),
            'scope': ['src/Gui', 'src/Mod'], 'special_types': list(SPECIAL), 'scanned_files': scanned,
            'gl_nodes': gl_nodes, 'special_node_creations': special,
            'external_gpu_candidates': external,
            'unreviewed': sorted(set(gl_nodes)-set(review)),
            'obsolete_review': sorted(set(review)-set(gl_nodes))}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('freecad_source', type=Path)
    parser.add_argument('--review', type=Path, default=Path(__file__).resolve().parents[2]/'docs/inventories/freecad-node-review.json')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path, help='Compare a recorded inventory, including source digest')
    args = parser.parse_args()
    if not all((args.freecad_source/'src'/area).is_dir() for area in ('Gui','Mod')):
        parser.error('Expected a FreeCAD checkout containing src/Gui and src/Mod')
    review = json.loads(args.review.read_text(encoding="utf-8")) if args.review.exists() else {}
    result = audit(args.freecad_source.resolve(), review)
    if args.output:
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"{len(result['gl_nodes'])} GL classes; {len(result['special_node_creations'])} special creations; {len(result['external_gpu_candidates'])} external GPU candidates; {result['scanned_files']} files")
    if result['unreviewed'] or result['obsolete_review']:
        print('Review mismatch:', result['unreviewed'], result['obsolete_review'])
        return 1
    if args.check and result != json.loads(args.check.read_text(encoding="utf-8")):
        print('Inventory differs: source or review changed; inspect and regenerate deliberately')
        return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
