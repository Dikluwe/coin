import ast
from pathlib import Path
import unittest


class DocumentChrome(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(Path(__file__).with_name('freecad_mouse_links.FCMacro').read_text())
        function = next(node for node in tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == 'chrome_rgb_error')
        self.ctx = {}
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<chrome>', 'exec'), self.ctx)

    def test_matching_qt_chrome(self):
        self.assertEqual(self.ctx['chrome_rgb_error']([((240, 240, 240), (240, 240, 240))]), 0)

    def test_bgfx_background_cannot_pass_as_document_tab_bar(self):
        self.assertGreater(self.ctx['chrome_rgb_error']([((32, 32, 32), (240, 240, 240))]), 6)

    def test_missing_samples_cannot_pass(self):
        with self.assertRaisesRegex(RuntimeError, 'no document chrome samples'):
            self.ctx['chrome_rgb_error']([])

    def test_resized_hover_is_a_separate_delayed_phase(self):
        tree = ast.parse(Path(__file__).with_name('freecad_mouse_links.FCMacro').read_text())
        functions = {node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)}
        self.assertIn('resized_hover', functions)
        baseline_calls = {node.func.id for node in ast.walk(functions['resized_baseline'])
                          if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)}
        self.assertNotIn('expect_preselection', baseline_calls)
        self.assertIn('step', baseline_calls)
