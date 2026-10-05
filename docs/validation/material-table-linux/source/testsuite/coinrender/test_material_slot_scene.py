"""CPU oracles for scene cardinality, float32 colors and source preservation."""

import hashlib
import importlib.util
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().parents[2] / "scripts/coinrender/generate_material_slot_scene.py"
SPEC = importlib.util.spec_from_file_location("material_slot_scene", SCRIPT)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)

PREFIX = """#Inventor V2.1 ascii
# Shared scene-graph geometry; GPU instancing is not implied.
Separator {
 LightModel { model PHONG }
 DirectionalLight { direction -0.5 -1 -0.3 intensity 0.85 }
 DirectionalLight { direction 0.7 -0.4 0.5 intensity 0.35 }
 Separator { Material { diffuseColor 0.16 0.21 0.23 }
 Translation { translation 0 -0.25 0 } Cube { width 16.00 height 0.5 depth 16.00 } }
"""
SOURCE = PREFIX + """ Separator {
  DEF CityMaterial0 Material { diffuseColor 0.72 0.78 0.85 shininess 0.2 }
  Transform { translation -3.100 2.000 -3.100 scaleFactor 1.200 4.000 1.300 }
  DEF CityBlock Cube { width 1 height 1 depth 1 }
 }
 Separator {
  DEF CityMaterial1 Material { diffuseColor 0.36 0.52 0.67 shininess 0.2 }
  Transform { translation -0.900 1.000 -3.100 scaleFactor 1.100 2.000 1.400 }
  USE CityBlock
 }
 Separator {
  USE CityMaterial0
  Transform { translation 1.300 1.500 -3.100 scaleFactor 1.600 3.000 1.200 }
  USE CityBlock
 }
 Separator {
  USE CityMaterial1
  Transform { translation 3.500 2.500 -3.100 scaleFactor 1.300 5.000 1.600 }
  USE CityBlock
 }
}
"""


def without_building_materials(data):
    # An independent byte oracle: remove just the replaceable source lines,
    # rather than using the generator's parser or layout hash.
    return re.sub(rb"^  (?:DEF CityMaterial[^\n]+|USE CityMaterial\d+)\n", b"", data, flags=re.M)


class MaterialSlotSceneTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "source.iv"
        self.source.write_text(SOURCE, encoding="ascii", newline="\n")

    def generate(self, slots, objects=None, name="result.iv"):
        path = self.directory / name
        result = GENERATOR.generate(self.source, path, slots, objects)
        return path, result

    def test_all_non_material_bytes_are_preserved_and_outputs_are_deterministic(self):
        first, metadata = self.generate(5)
        second, repeated = self.generate(5, name="another.iv")
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(metadata["scene"]["sha256"], repeated["scene"]["sha256"])
        self.assertEqual(without_building_materials(self.source.read_bytes()), without_building_materials(first.read_bytes()))
        self.assertEqual(metadata["source_city"]["sha256"], hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.assertEqual(metadata["layout"]["source_layout_sha256"], metadata["layout"]["output_layout_sha256"])
        self.assertEqual(first.read_text().count("DEF CityBlock Cube"), 1)
        self.assertEqual(first.read_text().count("USE CityBlock"), 3)

    def test_slots_include_ground_and_every_requested_palette_entry_is_used(self):
        path, metadata = self.generate(3)
        text = path.read_text()
        self.assertEqual(text.count("DEF CityMaterial"), 2)
        self.assertEqual(text.count("USE CityMaterial"), 2)
        self.assertEqual(text.count("Material {"), 3)
        self.assertEqual(metadata["expected_static"]["captured_material_slots"], 3)
        self.assertEqual(metadata["expected_static"]["logical_draws"], 5)
        self.assertEqual(metadata["expected_static"]["triangles"], 60)
        self.assertEqual(metadata["expected_static"]["wgpu_gpu_material_table_nominal_bytes"], 240)
        self.assertEqual(metadata["animation"]["materials-10"]["selected_buildings"], 1)
        self.assertEqual(metadata["animation"]["materials-100"]["selected_buildings"], 4)
        self.assertFalse(metadata["animation"]["captured_slot_count_guaranteed"])

    def test_palette_is_unique_after_literal_float32_round_trip_at_supported_limit(self):
        bits = set()
        for index in range(GENERATOR.MAX_OBJECTS):
            color = GENERATOR.material_color(index)
            literal = GENERATOR.color_literal(color)
            parsed = struct.pack("<3f", *(float(component) for component in literal.split()))
            self.assertEqual(parsed, struct.pack("<3f", *color))
            self.assertTrue(all(0.25 <= component <= 0.7421875 for component in color))
            bits.add(parsed)
        self.assertEqual(len(bits), GENERATOR.MAX_OBJECTS)

    def test_object_prefix_retains_transform_and_shared_cube_order(self):
        path, metadata = self.generate(3, objects=2)
        text = path.read_text()
        self.assertIn("translation -3.100 2.000 -3.100", text)
        self.assertIn("translation -0.900 1.000 -3.100", text)
        self.assertNotIn("translation 1.300", text)
        self.assertEqual(text.count("Transform {"), 2)
        self.assertEqual(metadata["layout"]["selection"], "source prefix")
        self.assertEqual(metadata["source_city"]["buildings"], 4)
        self.assertEqual(metadata["parameters"]["objects"], 2)

    def test_invalid_requested_counts_and_existing_evidence_do_not_write(self):
        for slots, objects in ((1, None), (6, None), (3, 0), (3, 5), (4, 2)):
            with self.subTest(slots=slots, objects=objects), self.assertRaises(ValueError):
                self.generate(slots, objects)
            self.assertFalse((self.directory / "result.iv").exists())
        output = self.directory / "result.iv"
        output.write_bytes(b"preserved evidence")
        with self.assertRaises(ValueError):
            self.generate(3)
        self.assertEqual(output.read_bytes(), b"preserved evidence")
        with self.assertRaises(ValueError):
            GENERATOR.generate(self.source, self.source, 3)
        self.assertEqual(self.source.read_text(), SOURCE)

    def test_unsupported_scene_states_and_alias_errors_fail_before_output(self):
        bad_sources = [
            SOURCE.replace("shininess 0.2", "shininess 0.5", 1),
            SOURCE.replace("shininess 0.2", "shininess 0.2 transparency 0.4", 1),
            SOURCE.replace("USE CityMaterial0", "USE CityMaterial77", 1),
            SOURCE.replace("DEF CityMaterial1", "DEF CityMaterial0", 1),
            SOURCE.replace("scaleFactor 1.200", "scaleFactor -1.200", 1),
            SOURCE.replace("translation -3.100 2.000", "translation -3.100 -2.000", 1),
            SOURCE.replace("scaleFactor 1.200", "scaleFactor 1e40", 1),
            SOURCE.replace("USE CityBlock", "DEF CityBlock Cube { width 1 height 1 depth 1 }", 1),
            SOURCE.replace("DEF CityBlock Cube { width 1 height 1 depth 1 }", "USE CityBlock", 1),
            SOURCE + "Material { diffuseColor 1 0 0 }\n",
            SOURCE.replace("\n", "\r\n"),
        ]
        for index, text in enumerate(bad_sources):
            with self.subTest(index=index):
                self.source.write_bytes(text.encode("ascii"))
                with self.assertRaises(ValueError):
                    self.generate(3)
                self.assertFalse((self.directory / "result.iv").exists())


if __name__ == "__main__":
    unittest.main()
