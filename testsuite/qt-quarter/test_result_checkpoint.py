import json
from pathlib import Path
import tempfile
import unittest
from run import checkpoint_results


class ResultCheckpoint(unittest.TestCase):
    def test_completed_cells_survive_an_interruption(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            completed = [{'status': 'PASS', 'renderer': 'vulkan', 'scale': 1}]
            checkpoint_results(directory, completed)
            self.assertEqual(json.loads((directory / 'results.json').read_text()), completed)
            self.assertFalse((directory / 'results.json.tmp').exists())

    def test_failure_is_preserved_not_converted_into_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            checkpoint_results(directory, [{'status': 'PASS'}])
            checkpoint_results(directory, [{'status': 'PASS'}, {'status': 'FAIL'}])
            self.assertEqual(json.loads((directory / 'results.json').read_text())[-1]['status'], 'FAIL')
