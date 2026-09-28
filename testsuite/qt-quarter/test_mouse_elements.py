"""Measure small vertex highlights without sparse pixel-sampling aliasing."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest


class MouseElements(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(Path(__file__).with_name('freecad_mouse_elements.FCMacro').read_text())
        nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == 'element_delta']
        self.context = {'viewer_widget': lambda: SimpleNamespace(
            viewport=lambda: SimpleNamespace(devicePixelRatioF=lambda: 1)),
            'delta': lambda a, b: 99}
        exec(compile(ast.Module(body=nodes, type_ignores=[]), '<elements macro>', 'exec'),
             self.context)
        self.point = SimpleNamespace(x=lambda: 16, y=lambda: 16)

    def image(self, changed=False, far=False):
        def pixel(x, y):
            return int(changed and (2 <= x < 4 and 2 <= y < 6 if far
                                   else 15 <= x < 17 and 15 <= y < 19))
        return SimpleNamespace(width=lambda: 32, height=lambda: 32, pixel=pixel)

    def test_all_eight_small_sprite_pixels_are_counted(self):
        self.assertEqual(self.context['element_delta'](
            self.image(), self.image(True), 'Vertex1', self.point), 8)

    def test_no_highlight_is_zero(self):
        self.assertEqual(self.context['element_delta'](
            self.image(), self.image(), 'Vertex1', self.point), 0)

    def test_unrelated_pixels_cannot_stand_in_for_vertex(self):
        self.assertEqual(self.context['element_delta'](
            self.image(), self.image(True, True), 'Vertex1', self.point), 0)

    def test_faces_and_edges_keep_the_existing_measurement(self):
        for name in ('Face1', 'Edge1'):
            self.assertEqual(self.context['element_delta'](
                self.image(), self.image(True), name, self.point), 99)
