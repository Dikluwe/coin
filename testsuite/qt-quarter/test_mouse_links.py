"""Native link-selection macro must reject synthetic or missing modifiers."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock


class LinkMouseInput(unittest.TestCase):
    def setUp(self):
        self.tree = ast.parse(Path(__file__).with_name('freecad_mouse_links.FCMacro').read_text())
        function = next(node for node in self.tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == 'key_click')
        self.report = {}
        self.ctx = {
            'active': lambda: None, 'display': object(), 'report': self.report,
            'wait_button_event': Mock(),
            'x11': SimpleNamespace(XKeysymToKeycode=lambda display, key: 37,
                                   XSync=Mock(), XFlush=Mock()),
            'xtst': SimpleNamespace(XTestFakeKeyEvent=Mock(return_value=1)),
            'QtWidgets': SimpleNamespace(QApplication=SimpleNamespace(processEvents=Mock())),
        }
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<links macro>', 'exec'),
             self.ctx)

    def test_ctrl_requires_coin_mouse_event_with_modifier(self):
        self.ctx['click'] = lambda: self.report.setdefault('button_events', []).append(
            {'CtrlDown': True, 'ShiftDown': False})
        self.ctx['key_click'](0xffe3)
        self.assertEqual(self.ctx['xtst'].XTestFakeKeyEvent.call_count, 2)

    def test_shift_requires_coin_mouse_event_with_modifier(self):
        self.ctx['click'] = lambda: self.report.setdefault('button_events', []).append(
            {'CtrlDown': False, 'ShiftDown': True})
        self.ctx['key_click'](0xffe1)

    def test_wrong_modifier_cannot_pass(self):
        self.ctx['click'] = lambda: self.report.setdefault('button_events', []).append(
            {'CtrlDown': False, 'ShiftDown': True})
        with self.assertRaisesRegex(RuntimeError, 'Coin did not receive'):
            self.ctx['key_click'](0xffe3)
        self.assertEqual(self.ctx['xtst'].XTestFakeKeyEvent.call_count, 2)

    def test_delayed_coin_event_is_acknowledged_before_modifier_release(self):
        self.ctx['click'] = lambda: None
        def acknowledge(prior):
            self.assertEqual(prior, 0)
            self.assertEqual(self.ctx['xtst'].XTestFakeKeyEvent.call_count, 1)
            self.report.setdefault('button_events', []).append({'CtrlDown': True})
        self.ctx['wait_button_event'].side_effect = acknowledge
        self.ctx['key_click'](0xffe3)
        self.ctx['wait_button_event'].assert_called_once_with(0)
        self.assertEqual(self.ctx['xtst'].XTestFakeKeyEvent.call_count, 2)

    def test_missing_coin_event_still_fails_and_releases_modifier(self):
        self.ctx['click'] = lambda: None
        with self.assertRaisesRegex(RuntimeError, 'Coin did not receive'):
            self.ctx['key_click'](0xffe3)
        self.assertEqual(self.ctx['xtst'].XTestFakeKeyEvent.call_count, 2)

    def test_base_object_cannot_pass_as_an_instance(self):
        function = next(node for node in self.tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == 'expect_selection')
        context = {'Gui': SimpleNamespace(Selection=SimpleNamespace(getSelectionEx=lambda: [
                       SimpleNamespace(ObjectName='Box', SubElementNames=['Face6'])])),
                   'targets': {'LinkA': ('Face6', None)}, 'report': {}}
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<link identity>', 'exec'), context)
        with self.assertRaisesRegex(RuntimeError, 'wrong link instances'):
            context['expect_selection'](('LinkA',))

    def test_selection_is_never_created_by_api(self):
        called = {node.func.attr for node in ast.walk(self.tree)
                  if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)}
        self.assertNotIn('addSelection', called)
        self.assertNotIn('setPreselection', called)
        self.assertNotIn('sendEvent', called)


class LinkInstancePixels(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(Path(__file__).with_name('freecad_mouse_links.FCMacro').read_text())
        function = next(node for node in tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == 'expect_instance_pixels')
        self.counts = {'LinkA': 1000, 'LinkB': 0}
        self.ctx = {'images': {'links-baseline': object(), 'snapshot': object()}, 'report': {},
                    'instance_region': lambda name, image: name,
                    'changes': lambda base, image, region: self.counts[region]}
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<link pixels>', 'exec'),
             self.ctx)

    def test_selected_instance_only(self):
        self.ctx['expect_instance_pixels']('snapshot', ('LinkA',))

    def test_shared_mesh_highlight_leak_cannot_pass(self):
        self.counts['LinkB'] = 1000
        with self.assertRaisesRegex(RuntimeError, 'leaked'):
            self.ctx['expect_instance_pixels']('snapshot', ('LinkA',))

    def test_missing_expected_instance_highlight_cannot_pass(self):
        self.counts['LinkA'] = 0
        with self.assertRaisesRegex(RuntimeError, 'missing'):
            self.ctx['expect_instance_pixels']('snapshot', ('LinkA',))

class LinkVisibleTarget(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(Path(__file__).with_name('freecad_mouse_links.FCMacro').read_text())
        function = next(node for node in tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == 'unobstructed')
        self.viewport = SimpleNamespace(mapToGlobal=lambda p: p,
                                        isAncestorOf=lambda widget: False)
        self.widget_at = Mock(return_value=self.viewport)
        self.ctx = {'viewer_widget': lambda: SimpleNamespace(viewport=lambda: self.viewport),
                    'QtWidgets': SimpleNamespace(QApplication=SimpleNamespace(widgetAt=self.widget_at))}
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<visible target>', 'exec'),
             self.ctx)

    def test_real_viewport_is_an_actionable_target(self):
        self.assertTrue(self.ctx['unobstructed'](object()))

    def test_overlay_button_cannot_be_used_as_a_part_target(self):
        self.widget_at.return_value = object()
        self.assertFalse(self.ctx['unobstructed'](object()))

    def test_button_child_of_viewport_cannot_intercept_a_part_click(self):
        self.viewport.isAncestorOf = lambda widget: True
        self.widget_at.return_value = SimpleNamespace(inherits=lambda name: name == 'QAbstractButton')
        self.assertFalse(self.ctx['unobstructed'](object()))

    def test_qt_owned_viewport_wrapper_is_accepted_by_native_ancestry(self):
        self.viewport.isAncestorOf = lambda widget: True
        self.widget_at.return_value = SimpleNamespace(inherits=lambda name: False)
        self.assertTrue(self.ctx['unobstructed'](object()))

    def test_outside_window_cannot_be_used_as_a_part_target(self):
        self.widget_at.return_value = None
        self.assertFalse(self.ctx['unobstructed'](object()))
