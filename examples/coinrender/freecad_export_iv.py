#!/usr/bin/env python3
"""Export selected final FreeCAD shapes as plain Inventor indexed meshes.

Run with the Python installation that can import the FreeCAD module. This
intentionally drops FreeCAD GUI-only nodes, selection overlays, and materials:
the result is a geometry interoperability test, not a viewport snapshot.
"""

# The FreeCAD initializer clears some names already imported in __main__.
# Import it first, then bind the helper modules used below.
import FreeCAD as App
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("document", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--object", action="append", required=True,
                        help="Final shape object name; repeat for several")
    parser.add_argument("--deflection", type=float, default=0.5)
    args = parser.parse_args()
    if args.deflection <= 0:
        parser.error("deflection must be positive")
    if not args.document.is_file():
        parser.error(f"document not found: {args.document}")

    doc = App.openDocument(str(args.document.resolve()))
    palette = ((0.77, 0.79, 0.83), (0.78, 0.55, 0.33),
               (0.43, 0.69, 0.78), (0.66, 0.78, 0.48))
    total_triangles = 0
    try:
        with args.output.open("w", encoding="ascii") as stream:
            stream.write("#Inventor V2.1 ascii\nSeparator {\n")
            for item, name in enumerate(args.object):
                obj = doc.getObject(name)
                if obj is None or not hasattr(obj, "Shape") or obj.Shape.isNull():
                    raise ValueError(f"{name}: missing non-null Shape")
                vertices, triangles = obj.Shape.tessellate(args.deflection)
                if not vertices or not triangles:
                    raise ValueError(f"{name}: tessellation is empty")
                color = palette[item % len(palette)]
                stream.write(f"  # {name}: {len(vertices)} vertices, "
                             f"{len(triangles)} triangles\n")
                stream.write("  Separator {\n"
                             "    ShapeHints { vertexOrdering COUNTERCLOCKWISE "
                             "shapeType SOLID }\n"
                             f"    Material {{ diffuseColor {color[0]} "
                             f"{color[1]} {color[2]} }}\n"
                             "    Coordinate3 { point [\n")
                for vertex in vertices:
                    stream.write(f"      {vertex.x:.9g} {vertex.y:.9g} "
                                 f"{vertex.z:.9g},\n")
                stream.write("    ] }\n    IndexedFaceSet { coordIndex [\n")
                for triangle in triangles:
                    if len(triangle) != 3 or any(index < 0 or index >= len(vertices)
                                                 for index in triangle):
                        raise ValueError(f"{name}: invalid triangle {triangle}")
                    stream.write(f"      {triangle[0]}, {triangle[1]}, "
                                 f"{triangle[2]}, -1,\n")
                stream.write("    ] }\n  }\n")
                total_triangles += len(triangles)
                print(f"{name}: {len(vertices)} vertices, "
                      f"{len(triangles)} triangles")
            stream.write("}\n")
    finally:
        App.closeDocument(doc.Name)
    print(f"exported {args.output}: {total_triangles} triangles")


if __name__ == "__main__":
    main()
