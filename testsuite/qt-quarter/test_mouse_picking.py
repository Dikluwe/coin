"""Negative controls for the actual native mouse macro's selection assertions."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from run_isolated import private_mount_present


class MousePicking(unittest.TestCase):
    def setUp(self):
        self.tree = ast.parse(Path(__file__).with_name('freecad_mouse_picking.FCMacro').read_text())
        self.selection = Mock()
        self.context = {'Gui': SimpleNamespace(Selection=self.selection), 'report': {},
                        'targets': [('Face6', None), ('Face3', None)]}
        nodes = [node for node in self.tree.body if isinstance(node, ast.FunctionDef)
                 and node.name in ('check_hover', 'selected', 'settle', 'idle')]
        exec(compile(ast.Module(body=nodes, type_ignores=[]), '<mouse macro>', 'exec'),
             self.context)

    def test_wrong_hover_face_cannot_pass(self):
        self.selection.getPreselection.return_value = SimpleNamespace(
            ObjectName='Box', SubElementNames=['Face2'])
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            self.context['check_hover']('Face3')

    def test_expected_face_on_wrong_object_cannot_pass(self):
        self.selection.getPreselection.return_value = SimpleNamespace(
            ObjectName='Other', SubElementNames=['Face3'])
        with self.assertRaises(RuntimeError):
            self.context['check_hover']('Face3')

    def test_wrong_clicked_face_cannot_pass(self):
        self.selection.getSelectionEx.return_value = [SimpleNamespace(
            ObjectName='Box', SubElementNames=['Face2'])]
        with self.assertRaisesRegex(RuntimeError, 'wrong face'):
            self.context['selected']()

    def test_no_direct_positive_selection_injection(self):
        forbidden = {'addSelection', 'setPreselection', 'sendEvent'}
        calls = [node.func.attr for node in ast.walk(self.tree)
                 if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)]
        self.assertFalse(forbidden.intersection(calls))
        self.assertIn('XTestFakeMotionEvent', calls)
        self.assertIn('XTestFakeButtonEvent', calls)
        extended = ast.parse(Path(__file__).with_name('freecad_mouse_elements.FCMacro').read_text())
        extended_calls = [node.func.attr for node in ast.walk(extended)
                          if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)]
        self.assertFalse(forbidden.intersection(extended_calls))

    def test_redraw_loop_cannot_be_hidden_by_settling(self):
        frame = [10]
        self.context.update(active=lambda: None, step=Mock(),
                            viewer_widget=lambda: SimpleNamespace(property=lambda name: frame[0]))
        self.context['report'].update(settle_samples=0, settle_stable=0, settle_frame=10)
        for _ in range(5):
            frame[0] += 1
            self.context['settle']()
        frame[0] += 1
        with self.assertRaisesRegex(RuntimeError, 'did not settle'):
            self.context['settle']()
        self.assertNotIn('idle_start', self.context['report'])

    def test_idle_rejects_any_extra_frame(self):
        self.context.update(viewer_widget=lambda: SimpleNamespace(property=lambda name: 11))
        self.context['report']['idle_start'] = 10
        with self.assertRaisesRegex(RuntimeError, 'continuous idle redraw'):
            self.context['idle']()

    def test_disconnected_private_fuse_is_detected_from_mount_table(self):
        with patch('run_isolated.Path.read_text', return_value=
                   '11 1 0:3 / /tmp/owned/runtime/gvfs rw - fuse gvfs rw\n'):
            self.assertTrue(private_mount_present('/tmp/owned/runtime/gvfs'))
            self.assertFalse(private_mount_present('/tmp/another/runtime/gvfs'))
