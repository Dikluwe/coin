"""Generate a deterministic Open Inventor city for backend scaling measurements."""
import argparse
import json
from pathlib import Path
import random


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--grid", type=int, default=200)
    parser.add_argument("--seed", type=int, default=136)
    args = parser.parse_args()
    if not 1 <= args.grid <= 1000:
        parser.error("grid must be between 1 and 1000")
    rng = random.Random(args.seed)
    palette = [(0.72, 0.78, 0.85), (0.36, 0.52, 0.67), (0.82, 0.65, 0.43),
               (0.53, 0.65, 0.59), (0.70, 0.45, 0.34), (0.53, 0.48, 0.65),
               (0.83, 0.82, 0.73), (0.32, 0.43, 0.52)]
    span = args.grid * 2.2 + (args.grid // 10) * 2.5
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as out:
        out.write("#Inventor V2.1 ascii\n# Shared scene-graph geometry; GPU instancing is not implied.\nSeparator {\n")
        out.write(" LightModel { model PHONG }\n")
        out.write(" DirectionalLight { direction -0.5 -1 -0.3 intensity 0.85 }\n")
        out.write(" DirectionalLight { direction 0.7 -0.4 0.5 intensity 0.35 }\n")
        out.write(" Separator { Material { diffuseColor 0.16 0.21 0.23 }\n")
        out.write(f" Translation {{ translation 0 -0.25 0 }} Cube {{ width {span + 6:.2f} height 0.5 depth {span + 6:.2f} }} }}\n")
        for index in range(args.grid * args.grid):
            x, z = index % args.grid, index // args.grid
            px = x * 2.2 + (x // 10) * 2.5 - (span - 2.2) / 2
            pz = z * 2.2 + (z // 10) * 2.5 - (span - 2.2) / 2
            height = rng.uniform(1.2, 7.0) * (2.0 if index % 31 == 0 else 1.0)
            width, depth = rng.uniform(1.1, 1.7), rng.uniform(1.1, 1.7)
            out.write(" Separator {\n")
            if index < len(palette):
                color = " ".join(str(component) for component in palette[index])
                out.write(f"  DEF CityMaterial{index} Material {{ diffuseColor {color} shininess 0.2 }}\n")
            else:
                out.write(f"  USE CityMaterial{index % len(palette)}\n")
            out.write(f"  Transform {{ translation {px:.3f} {height / 2:.3f} {pz:.3f} scaleFactor {width:.3f} {height:.3f} {depth:.3f} }}\n")
            out.write("  DEF CityBlock Cube { width 1 height 1 depth 1 }\n" if index == 0 else "  USE CityBlock\n")
            out.write(" }\n")
        out.write("}\n")
    print(json.dumps({"scene": str(args.output), "seed": args.seed,
                      "buildings": args.grid ** 2, "triangles": 12 * (args.grid ** 2 + 1),
                      "bytes": args.output.stat().st_size}))


if __name__ == "__main__":
    main()
