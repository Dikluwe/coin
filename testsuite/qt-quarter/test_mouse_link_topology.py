"""Do not collapse assembly paths or confuse source geometry with instances."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock


class LinkedTopology(unittest.TestCase):
    def setUp(self):
        self.tree = ast.parse(Path(__file__).with_name('freecad_mouse_link_topology.FCMacro').read_text())
        functions = [node for node in self.tree.body if isinstance(node, ast.FunctionDef)
                     and node.name in ('pick_identity', 'expect_identity', 'selection_identities')]
        self.selection = Mock(return_value=[])
        self.ctx = {'Gui': SimpleNamespace(Selection=SimpleNamespace(getSelectionEx=self.selection)),
                    'doc': SimpleNamespace(Name='OwnedDocument')}
        exec(compile(ast.Module(body=functions, type_ignores=[]), '<linked topology>', 'exec'), self.ctx)

    def test_assembly_pick_preserves_parent_and_entire_subpath(self):
        self.assertEqual(self.ctx['pick_identity']({'ParentObject': SimpleNamespace(Name='LinkA'),
                         'SubName': 'SubAssembly.Box.Edge3', 'Object': 'Box', 'Component': 'Edge3'}),
                         ('LinkA', 'SubAssembly.Box.Edge3'))

    def test_direct_link_pick(self):
        self.assertEqual(self.ctx['pick_identity']({'Object': 'LinkB', 'Component': 'Vertex4'}),
                         ('LinkB', 'Vertex4'))

    def test_missing_pick_is_not_an_identity(self):
        self.assertIsNone(self.ctx['pick_identity'](None))

    def test_source_geometry_cannot_pass_as_assembly_instance(self):
        with self.assertRaisesRegex(RuntimeError, 'wrong linked topology identity'):
            self.ctx['expect_identity'](('Box', 'Edge3'), ('LinkA', 'SubAssembly.Box.Edge3'))

    def test_partial_subpath_cannot_pass(self):
        with self.assertRaises(RuntimeError):
            self.ctx['expect_identity'](('LinkA', 'Edge3'), ('LinkA', 'SubAssembly.Box.Edge3'))

    def test_selection_explicitly_disables_source_resolution(self):
        self.selection.return_value = [SimpleNamespace(ObjectName='LinkB',
                                                       SubElementNames=['SubAssembly.Box.Vertex4'])]
        self.assertEqual(self.ctx['selection_identities'](), [('LinkB', ('SubAssembly.Box.Vertex4',))])
        self.selection.assert_called_once_with('OwnedDocument', 0)

    def test_positive_selection_is_never_injected_by_api(self):
        calls = {node.func.attr for node in ast.walk(self.tree)
                 if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)}
        self.assertFalse({'addSelection', 'setPreselection', 'sendEvent'} & calls)

    def test_fixture_covers_nested_shape_and_nested_assembly(self):
        assignment = next(node for node in self.tree.body if isinstance(node, ast.Assign)
                          and any(isinstance(target, ast.Name) and target.id == 'scenarios'
                                  for target in node.targets))
        self.assertEqual(ast.literal_eval(assignment.value),
                         ('nested-shape', 'assembly', 'nested-assembly'))

    def test_nested_links_compose_placements(self):
        function = next(node for node in self.tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == 'next_scenario')
        assignment = next(node for node in ast.walk(function) if isinstance(node, ast.Assign)
                          and any(isinstance(target, ast.Attribute) and target.attr == 'LinkTransform'
                                  for target in node.targets))
        self.assertTrue(eval(compile(ast.Expression(assignment.value), '<transform>', 'eval'),
                             {'scenario': 'nested-shape'}))
        self.assertTrue(eval(compile(ast.Expression(assignment.value), '<transform>', 'eval'),
                             {'scenario': 'nested-assembly'}))
        self.assertFalse(eval(compile(ast.Expression(assignment.value), '<transform>', 'eval'),
                              {'scenario': 'assembly'}))

    def test_topology_capture_memory_is_bounded(self):
        function = next(node for node in self.tree.body
                        if isinstance(node, ast.FunctionDef) and node.name == 'topology_clean')
        self.assertTrue(any(isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                            and isinstance(node.func.value, ast.Name)
                            and node.func.value.id == 'images' and node.func.attr == 'clear'
                            for node in ast.walk(function)))
