import importlib.util
import tempfile
import sys
sys.dont_write_bytecode = True
import unittest
from pathlib import Path
from unittest.mock import patch

script = Path(__file__).resolve().parents[2]/'scripts/coinrender/audit_freecad_nodes.py'
spec = importlib.util.spec_from_file_location('node_inventory', script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class NodeInventoryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root/'src/Gui').mkdir(parents=True)
        (self.root/'src/Mod/Test').mkdir(parents=True)
        self.addCleanup(self.temp.cleanup)
        self.revision = patch.object(module, 'revision', return_value='fixture')
        self.revision.start()
        self.addCleanup(self.revision.stop)

    def audit(self, review=None):
        return module.audit(self.root, review or {})

    def test_path_overrides_and_real_creations_exclude_comments_and_strings(self):
        (self.root/'src/Gui/Nodes.cpp').write_text('''
// void Fiction::GLRender(A* a) { new SoImage; }
const char* description = "Ignored::GLRender(A* a) { new SoText2; }";
void Custom::GLRenderInPath(A* a) { inherited::GLRenderInPath(a); }
void Custom::callback(A* a) { a->traverse(callbackRoot); }
void Custom::generatePrimitives(A* a) { /* intentionally empty */ }
auto* bitmap = new SoImage;
''')
        (self.root/'src/Mod/Test/view.py').write_text('''
# icon = coin.SoImage()
text = "coin.SoTexture3()"
label = coin.SoText2()
''')
        result = self.audit({'Custom': {'status': 'PARTIAL'}})
        self.assertEqual(list(result['gl_nodes']), ['Custom'])
        self.assertEqual(result['unreviewed'], [])
        methods = result['gl_nodes']['Custom']['methods']
        self.assertFalse(methods[1]['empty_body'])
        self.assertTrue(methods[2]['empty_body'])
        self.assertEqual([v['node'] for v in result['special_node_creations']], ['SoImage', 'SoText2'])

    def test_new_and_removed_classes_require_review_and_content_change_invalidates_snapshot(self):
        source = self.root/'src/Gui/New.cpp'
        source.write_text('void NewlyAdded::GLRenderBelowPath(A* a) {}\n')
        before = self.audit({'Removed': {'status': 'BLOCKED'}})
        self.assertEqual(before['unreviewed'], ['NewlyAdded'])
        self.assertEqual(before['obsolete_review'], ['Removed'])
        source.write_text(source.read_text()+'// unrelated source change still requires inspection\n')
        after = self.audit({'Removed': {'status': 'BLOCKED'}})
        self.assertNotEqual(before['source_digest'], after['source_digest'])

    def test_external_shader_widget_is_not_a_coin_node(self):
        (self.root/'src/Mod/Test/Shader.cpp').write_text('void build() { glCreateShader(0); glUseProgram(1); }')
        result = self.audit()
        self.assertEqual(result['gl_nodes'], {})
        self.assertEqual(result['external_gpu_candidates'][0]['mechanisms'], ['glCreateShader','glUseProgram'])

if __name__ == '__main__':
    unittest.main()
