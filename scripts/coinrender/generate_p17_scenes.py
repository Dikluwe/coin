#!/usr/bin/env python3
"""Emit the two deterministic Coin scene graphs used by the P17 benchmarks."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def opaque_interleaved():
    colors = ((0.82, 0.19, 0.14), (0.16, 0.67, 0.29),
              (0.17, 0.33, 0.86), (0.82, 0.68, 0.15))
    objects = []
    for y in range(6):
        for x in range(6):
            color = colors[(x + y * 6) % len(colors)]
            objects.append('''Separator {
  Material { diffuseColor %.4f %.4f %.4f }
  Translation { translation %.4f %.4f %.4f }
  Cube { width 0.54 height 0.54 depth 0.54 }
}''' % (*color, (x - 2.5) * 0.69, (y - 2.5) * 0.69,
               (x + y) % 3 * 0.02))
    return '#Inventor V2.1 ascii\nSeparator {\n' + '\n'.join(objects) + '\n}\n'


def transparent_overlap():
    objects = []
    colors = ((.95, .16, .10), (.08, .72, .28),
              (.10, .34, .95), (.92, .72, .08))
    def quad(color, alpha, cx, cy, z, half):
        points = [(cx-half, cy-half, z), (cx+half, cy-half, z),
                  (cx+half, cy+half, z), (cx-half, cy+half, z)]
        values = ', '.join('%.6f %.6f %.6f' % point for point in points)
        return '''Separator {
  Material { diffuseColor %.4f %.4f %.4f transparency %.4f }
  Coordinate3 { point [ %s ] }
  IndexedFaceSet { coordIndex [ 0, 1, 2, 3, -1 ] }
}''' % (*color, 1.0-alpha, values)
    objects.append(quad((.08, .10, .16), 1.0, 0, 0, -2.0, 3.2))
    for layer in range(6):
        for item in range(8):
            angle = item * .78539816339 + layer * .19
            radius = .35 + (item % 4) * .48
            objects.append(quad(colors[(layer + item) % 4], .22 + .08 * layer,
                                math.cos(angle) * radius, math.sin(angle) * radius,
                                -1.2 + layer * .42 + (item % 2) * .08,
                                .72 + .06 * (item % 3)))
    return '#Inventor V2.1 ascii\nSeparator {\n' + '\n'.join(objects) + '\n}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for name, source, objects in (('opaque-interleaved', opaque_interleaved(), 36),
                                  ('transparent-overlap', transparent_overlap(), 49)):
        path = args.output_dir / (name + '.iv')
        path.write_text(source, encoding='ascii')
        manifest[name] = {'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                          'objects': objects}
    (args.output_dir / 'scenes.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
