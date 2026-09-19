"""FreeCAD GUI smoke for the bump-render SbSmallMap lab candidate.

Run this module through FreeCAD's GUI test runner.  ``COIN_LAB_LIBRARY`` must
be the absolute path of the expected libCoin shared library.  Screenshots are
written below ``COIN_LAB_OUTPUT_DIR`` (``/tmp`` by default).
"""

import os
import struct
import unittest
import zlib
from pathlib import Path

import FreeCAD as App
import FreeCADGui as Gui
import Part
from pivy import coin


OUTPUT_DIR = Path(os.environ.get("COIN_LAB_OUTPUT_DIR", "/tmp"))
OUT_A = OUTPUT_DIR / "coin-freecad-bump-smallmap-view-a.png"
OUT_B = OUTPUT_DIR / "coin-freecad-bump-smallmap-view-b.png"
OUT_A_REPEAT = OUTPUT_DIR / "coin-freecad-bump-smallmap-view-a-repeat.png"


def log(message):
    line = "COIN_BUMP_SMALLMAP_SMOKE: " + str(message)
    print(line, flush=True)
    App.Console.PrintMessage(line + "\n")


def loaded_coin_libraries():
    paths = set()
    with open("/proc/self/maps", "r", encoding="utf-8") as maps:
        for line in maps:
            if "libCoin.so" in line:
                paths.add(line.rstrip().split()[-1])
    return sorted(paths)


def expected_coin_library():
    value = os.environ.get("COIN_LAB_LIBRARY")
    if not value:
        raise AssertionError("COIN_LAB_LIBRARY must name the expected libCoin shared library")
    if not os.path.isabs(value):
        raise AssertionError("COIN_LAB_LIBRARY must be an absolute path")
    return os.path.realpath(value)


def paeth(a, b, c):
    prediction = a + b - c
    distance_a = abs(prediction - a)
    distance_b = abs(prediction - b)
    distance_c = abs(prediction - c)
    if distance_a <= distance_b and distance_a <= distance_c:
        return a
    if distance_b <= distance_c:
        return b
    return c


def rgb_pixels(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("screenshot is not a PNG: %s" % path)

    width = height = bit_depth = color_type = interlace = None
    compressed = bytearray()
    offset = 8
    while offset < len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        name = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if name == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
        elif name == b"IDAT":
            compressed.extend(payload)
        elif name == b"IEND":
            break

    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise AssertionError(
            "unsupported PNG layout: depth=%r color=%r interlace=%r"
            % (bit_depth, color_type, interlace)
        )

    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    expected_size = height * (stride + 1)
    if len(raw) != expected_size:
        raise AssertionError("unexpected PNG payload size")

    decoded = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        scanline = bytearray(raw[offset + 1 : offset + 1 + stride])
        offset += stride + 1
        for index in range(stride):
            left = scanline[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                scanline[index] = (scanline[index] + left) & 0xFF
            elif filter_type == 2:
                scanline[index] = (scanline[index] + above) & 0xFF
            elif filter_type == 3:
                scanline[index] = (scanline[index] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                scanline[index] = (
                    scanline[index] + paeth(left, above, upper_left)
                ) & 0xFF
            elif filter_type != 0:
                raise AssertionError("unsupported PNG filter %d" % filter_type)
        for index in range(0, stride, channels):
            decoded.append(tuple(scanline[index : index + 3]))
        previous = scanline
    return width, height, decoded


def foreground_pixel_count(path):
    width, height, pixels = rgb_pixels(path)
    count = sum(
        1
        for red, green, blue in pixels
        if green >= red + 30 and blue >= red + 60 and blue >= 100
    )
    return width, height, count


class CoinBumpSmallMapSmoke(unittest.TestCase):
    def render_view(self, view, output, label):
        view.viewAxonometric()
        view.fitAll()
        Gui.updateGui()
        view.redraw()
        Gui.updateGui()
        view.saveImage(str(output), 640, 480, "Current")
        Gui.updateGui()
        size = output.stat().st_size
        width, height, foreground = foreground_pixel_count(output)
        self.assertEqual((width, height), (640, 480))
        self.assertGreater(
            foreground,
            500,
            "%s screenshot contains no substantial blue foreground object" % label,
        )
        log(
            "%s screenshot=%s bytes=%d foreground_pixels=%d"
            % (label, output, size, foreground)
        )

    def test_two_view_a_b_a_bump_render(self):
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        expected = expected_coin_library()
        libraries = loaded_coin_libraries()
        resolved_libraries = [os.path.realpath(path) for path in libraries]
        log("FreeCAD version=" + ".".join(App.Version()[:3]))
        log("loaded libCoin=" + (", ".join(libraries) if libraries else "NONE"))
        self.assertIn(expected, resolved_libraries)

        doc = App.newDocument("CoinBumpSmallMapSmoke")
        try:
            box = doc.addObject("Part::Feature", "BumpBox")
            box.Shape = Part.makeBox(10.0, 10.0, 10.0)
            box.ViewObject.ShapeColor = (0.25, 0.62, 0.90)
            box.ViewObject.ShapeMaterial.SpecularColor = (0.9, 0.9, 0.9)
            box.ViewObject.ShapeMaterial.Shininess = 75.0
            doc.recompute()

            bump = coin.SoBumpMap()
            bump.image.setValue(coin.SbVec2s(2, 2), 1, bytes((0, 255, 255, 0)))
            box.ViewObject.RootNode.insertChild(bump, 0)

            gui_doc = Gui.getDocument(doc.Name)
            self.assertIsNotNone(gui_doc)
            first_view = gui_doc.activeView()
            second_view = gui_doc.createView("Gui::View3DInventor")
            self.assertIsNotNone(first_view)
            self.assertIsNotNone(second_view)

            context_a = (
                first_view.getViewer()
                .getSoRenderManager()
                .getGLRenderAction()
                .getCacheContext()
            )
            context_b = (
                second_view.getViewer()
                .getSoRenderManager()
                .getGLRenderAction()
                .getCacheContext()
            )
            log("view cache contexts A=%d B=%d" % (context_a, context_b))

            Gui.runCommand("Std_ActivatePrevWindow")
            Gui.updateGui()
            self.render_view(gui_doc.activeView(), OUT_A, "view-a")
            Gui.runCommand("Std_ActivateNextWindow")
            Gui.updateGui()
            self.render_view(gui_doc.activeView(), OUT_B, "view-b")
            Gui.runCommand("Std_ActivatePrevWindow")
            Gui.updateGui()
            self.render_view(gui_doc.activeView(), OUT_A_REPEAT, "view-a-repeat")
            log("PASS two-view A-B-A bump-map render")
        finally:
            App.closeDocument(doc.Name)
