"""Exercise the actual macro's bounded settling and strict idle check without Qt."""
import ast
from pathlib import Path
import unittest


class MenuIdle(unittest.TestCase):
    def setUp(self):
        self.frame = 10
        self.queue = []
        self.finished = False
        class Viewer:
            def property(viewer, name):
                return self.frame
        self.context = {
            'report': {'settle_samples': 0, 'settle_stable': 0, 'settle_frame': 10},
            'viewer_widget': lambda: Viewer(), 'native': True,
            'require_active': lambda: None,
            'step': lambda callback, delay: self.queue.append((callback, delay)),
            'finish': lambda: setattr(self, 'finished', True),
        }
        nodes = []
        for filename, names in [('freecad_selection_menu.FCMacro', ('settle', 'menu_idle')),
                                ('freecad_path_selection.FCMacro', ('idle',))]:
            tree = ast.parse(Path(__file__).with_name(filename).read_text())
            nodes += [node for node in tree.body
                      if isinstance(node, ast.FunctionDef) and node.name in names]
        exec(compile(ast.Module(body=nodes, type_ignores=[]), '<actual macros>', 'exec'),
             self.context)

    def sample(self):
        self.context['settle']()

    def test_two_quiet_samples_are_required(self):
        self.sample()
        self.assertNotIn('idle_start', self.context['report'])
        self.sample()
        self.assertEqual(self.context['report']['idle_start'], 10)
        self.assertEqual(self.queue[-1][1], 1200)

    def test_late_frame_restarts_settling(self):
        self.sample()
        self.frame += 1
        self.sample()
        self.sample()
        self.assertNotIn('idle_start', self.context['report'])
        self.sample()
        self.assertEqual(self.context['report']['idle_start'], 11)

    def test_continuous_redraw_cannot_pass(self):
        for _ in range(5):
            self.frame += 1
            self.sample()
        self.frame += 1
        with self.assertRaisesRegex(RuntimeError, 'did not settle'):
            self.sample()
        self.assertNotIn('idle_start', self.context['report'])

    def test_idle_remains_strict_after_settling(self):
        self.sample()
        self.sample()
        self.frame += 1
        with self.assertRaisesRegex(RuntimeError, 'continuous redraw'):
            self.context['menu_idle']()
        self.assertFalse(self.finished)
