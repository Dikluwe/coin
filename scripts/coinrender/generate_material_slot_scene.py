#!/usr/bin/env python3
"""Replace only the material palette of the deterministic benchmark city.

The accepted input is the narrow ASCII city format produced by
examples/coinrender/generate_large_scene.py. This is not a general Inventor
parser. Lights, ground, object transforms, shared Cube and traversal order are
preserved. --material-slots counts the building palette plus the ground.
No Coin library, renderer, benchmark, build or Git command is invoked.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


MAX_OBJECTS = 64000
MAX_SOURCE_BYTES = 32 * 1024 * 1024
FLOAT = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
TRIPLE = rf"{FLOAT} {FLOAT} {FLOAT}"
LIGHT = rf" DirectionalLight \{{ direction {TRIPLE} intensity {FLOAT} \}}\n"
PREFIX = re.compile(
    rf"#Inventor V2\.1 ascii\n(?:#[^\n]*\n)*Separator \{{\n"
    rf" LightModel \{{ model PHONG \}}\n{LIGHT}{LIGHT}"
    rf" Separator \{{ Material \{{ diffuseColor (?P<ground>{TRIPLE}) \}}\n"
    rf" Translation \{{ translation (?P<ground_position>{TRIPLE}) \}} "
    rf"Cube \{{ width (?P<ground_width>{FLOAT}) height (?P<ground_height>{FLOAT}) depth (?P<ground_depth>{FLOAT}) \}} \}}\n")
OBJECT = re.compile(
    rf" Separator \{{\n(?P<material>  [^\n]+\n)"
    rf"(?P<transform>  Transform \{{ translation (?P<position>{TRIPLE}) scaleFactor (?P<scale>{TRIPLE}) \}}\n)"
    rf"(?P<cube>  (?:DEF CityBlock Cube \{{ width 1 height 1 depth 1 \}}|USE CityBlock)\n) \}}\n")
MATERIAL_DEF = re.compile(
    rf"  DEF CityMaterial(?P<id>\d+) Material \{{ diffuseColor (?P<color>{TRIPLE}) shininess (?P<shininess>{FLOAT}) \}}\n")
MATERIAL_USE = re.compile(r"  USE CityMaterial(?P<id>\d+)\n")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def float32(value):
    try:
        result = struct.unpack("<f", struct.pack("<f", float(value)))[0]
    except (ValueError, OverflowError, struct.error) as error:
        raise ValueError("Value is not a finite float32: " + str(value)) from error
    require(result == result and abs(result) != float("inf"), "Nonfinite float32")
    return result


def vector(text):
    return tuple(float32(component) for component in text.split())


def color_key(color):
    return struct.pack("<3f", *color)


def material_color(index):
    # An odd multiplication permutes all 18-bit integers. Each six-bit lane
    # is encoded as an exactly representable float32 in [0.25, 0.7421875].
    # The existing +/-0.18 diffuse animation therefore has room before clamp.
    code = (index * 2654435761 + 12345) & ((1 << 18) - 1)
    return tuple(0.25 + ((code >> (6 * axis)) & 63) / 128 for axis in range(3))


def color_literal(color):
    result = " ".join(format(component, ".9g") for component in color)
    require(color_key(vector(result)) == color_key(color), "Color literal does not round-trip to float32")
    return result


def parse_city(data):
    require(len(data) <= MAX_SOURCE_BYTES, "Source city exceeds 32 MiB")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("Source must be ASCII Inventor, without BOM") from error
    require("\r" not in text, "Source city must use LF line endings")
    first = text.find(" Separator {\n")
    require(first >= 0, "Source contains no building Separators")
    prefix, cursor = text[:first], first
    header = PREFIX.fullmatch(prefix)
    require(header is not None, "Source header/lights/ground do not match the deterministic city profile")
    ground = vector(header["ground"])
    require(all(0 <= value <= 1 for value in ground), "Ground diffuse color is outside [0,1]")
    require(vector(header["ground_position"])[1] < 0, "Ground must remain below building centers")
    require(all(float32(header[name]) > 0 for name in ("ground_width", "ground_height", "ground_depth")), "Ground dimensions must be positive")
    objects, defined, colors = [], set(), []
    while text.startswith(" Separator {\n", cursor):
        match = OBJECT.match(text, cursor)
        require(match is not None, "Building does not contain exactly Material, Transform and Cube at byte " + str(cursor))
        definition, use = MATERIAL_DEF.fullmatch(match["material"]), MATERIAL_USE.fullmatch(match["material"])
        require(definition is not None or use is not None, "Unsupported building material syntax")
        if definition:
            material_id = int(definition["id"])
            require(material_id not in defined, "Duplicate CityMaterial DEF")
            require(float32(definition["shininess"]) == float32(0.2), "Building shininess must be the common 0.2")
            color = vector(definition["color"])
            require(all(0 <= value <= 1 for value in color), "Building diffuse color outside [0,1]")
            defined.add(material_id)
            colors.append(color)
        else:
            require(int(use["id"]) in defined, "CityMaterial USE precedes its DEF")
        position, scale = vector(match["position"]), vector(match["scale"])
        require(position[1] > 0 and all(abs(value) <= 32768 for value in position), "Building position outside the finite positive-center profile")
        require(all(0 < value <= 32768 for value in scale), "Building scale must be finite and positive")
        expected_cube = "  DEF CityBlock Cube { width 1 height 1 depth 1 }\n" if not objects else "  USE CityBlock\n"
        require(match["cube"] == expected_cube, "Only the first building may define the shared unit Cube")
        objects.append({"material": match["material"], "transform": match["transform"], "cube": match["cube"]})
        require(len(objects) <= MAX_OBJECTS, "Source has more than 64,000 buildings")
        cursor = match.end()
    require(text[cursor:] == "}\n", "Unexpected trailing scene content")
    return {"prefix": prefix, "objects": objects, "suffix": text[cursor:], "ground_color": ground,
            "source_palette_definitions": len(defined), "source_palette_unique_colors": len({color_key(color) for color in colors})}


def layout_digest(city, count):
    digest = hashlib.sha256(city["prefix"].encode("ascii"))
    for item in city["objects"][:count]:
        digest.update((" Separator {\n" + item["transform"] + item["cube"] + " }\n").encode("ascii"))
    digest.update(city["suffix"].encode("ascii"))
    return digest.hexdigest()


def generate(source, output, material_slots, objects=None, manifest_path=None):
    source, output = Path(source).resolve(), Path(output).resolve()
    manifest_path = Path(manifest_path).resolve() if manifest_path else output.with_name(output.name + ".manifest.json")
    require(output.suffix.lower() == ".iv" and manifest_path.suffix.lower() == ".json", "Use .iv output and .json manifest")
    require(len({source, output, manifest_path}) == 3, "Scene source/output/manifest paths must differ")
    require(not output.exists() and not manifest_path.exists(), "Scene output and manifest must be fresh paths")
    require(source.stat().st_size <= MAX_SOURCE_BYTES, "Source city exceeds 32 MiB")
    data = source.read_bytes()
    city = parse_city(data)
    count = len(city["objects"]) if objects is None else objects
    require(type(count) is int and 1 <= count <= len(city["objects"]), "--objects must select a positive prefix of source buildings")
    require(type(material_slots) is int and 2 <= material_slots <= count + 1, "--material-slots must be in [2, objects+1], including ground")
    palette_size = material_slots - 1
    palette = [material_color(index) for index in range(palette_size)]
    unique_colors = {color_key(color) for color in palette}
    require(len(unique_colors) == palette_size, "Palette has float32 collisions")
    require(color_key(city["ground_color"]) not in unique_colors, "Generated palette collides with the retained ground color")
    literals = [color_literal(color) for color in palette]
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="ascii", newline="\n") as stream:
        stream.write(city["prefix"])
        for index, item in enumerate(city["objects"][:count]):
            material = index % palette_size
            stream.write(" Separator {\n")
            if index < palette_size:
                stream.write(f"  DEF CityMaterial{material} Material {{ diffuseColor {literals[material]} shininess 0.2 }}\n")
            else:
                stream.write(f"  USE CityMaterial{material}\n")
            stream.write(item["transform"] + item["cube"] + " }\n")
        stream.write(city["suffix"])
    result_data = output.read_bytes()
    result_city = parse_city(result_data)
    original_layout = layout_digest(city, count)
    require(layout_digest(result_city, count) == original_layout, "Non-material source bytes changed")
    require(result_city["source_palette_definitions"] == palette_size and result_city["source_palette_unique_colors"] == palette_size,
            "Serialized scene palette differs from the requested count")
    metadata = {
        "schema_version": 1,
        "scene": {"path": str(output), "sha256": hashlib.sha256(result_data).hexdigest(), "bytes": len(result_data)},
        "source_city": {"path": str(source), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data), "buildings": len(city["objects"])},
        "generator": {"path": str(Path(__file__).resolve()), "sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "python": sys.version},
        "parameters": {"objects": count, "material_slots": material_slots},
        "layout": {"selection": "all source buildings" if count == len(city["objects"]) else "source prefix",
                   "non_material_bytes_preserved": True, "source_layout_sha256": original_layout,
                   "output_layout_sha256": layout_digest(result_city, count)},
        "expected_static": {"eligible_buildings": count, "ground_draws": 1, "logical_draws": count + 1,
                            "triangles": 12 * (count + 1), "shared_building_cube_nodes": 1, "total_cube_nodes": 2,
                            "building_material_nodes": palette_size, "total_material_nodes": material_slots,
                            "captured_material_slots": material_slots, "wgpu_gpu_material_table_nominal_bytes": 80 * material_slots},
        "colors": {"unique_building_float32_rgb": palette_size, "float32_round_trip_literals": True,
                   "minimum_component": min(value for color in palette for value in color),
                   "maximum_component": max(value for color in palette for value in color), "shininess": 0.2, "opaque": True},
        "animation": {"materials-10": {"selected_buildings": (count * 10 + 99) // 100, "prepared_clones": (count * 10 + 99) // 100},
                      "materials-100": {"selected_buildings": count, "prepared_clones": count},
                      "captured_slot_count_guaranteed": False,
                      "note": "Animation clones selected occurrences and changes diffuseColor. Full captures deduplicate current bytes; overlays can retain existing slots. Measure the real GPU table per frame."},
        "limits": ["Static counts are predictions for the unchanged generated scene, not observed GPU results.",
                   "Shared Cube nodes do not guarantee shared captured ranges or a fixed number of GPU draws; material slots can increase source geometry.",
                   "No renderer, benchmark, build or Git command was invoked."],
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    with manifest_path.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(metadata, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-city", type=Path, required=True)
    parser.add_argument("--objects", type=int, help="Keep this many source buildings, in traversal order; default: all")
    parser.add_argument("--material-slots", type=int, default=40001, help="Static building palette plus the retained ground")
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    try:
        result = generate(args.source_city, args.output, args.material_slots, args.objects, args.manifest)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps({"scene": result["scene"], "parameters": result["parameters"], "expected_static": result["expected_static"]}, indent=2))


if __name__ == "__main__":
    main()
