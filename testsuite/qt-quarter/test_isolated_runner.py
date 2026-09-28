"""Cleanup must only signal processes owned by the isolated runner."""
import ast
from pathlib import Path
import signal
import subprocess
import unittest
from unittest.mock import Mock, patch

from run_isolated import stop, screen_dimensions


class IsolatedCleanup(unittest.TestCase):
    def test_gpu_compositor_has_no_host_input_surface(self):
        tree = ast.parse(Path(__file__).with_name('run_isolated.py').read_text())
        commands = [node.value.args[0] for node in ast.walk(tree)
                    if isinstance(node, ast.Assign)
                    and any(isinstance(target, ast.Name) and target.id == 'compositor'
                            for target in node.targets)
                    and isinstance(node.value, ast.Call)]
        self.assertEqual(len(commands), 1)
        options = [node.value for node in commands[0].elts if isinstance(node, ast.Constant)]
        self.assertIn('--backend=headless', options)
        self.assertNotIn('--backend=x11', options)
        self.assertIn('--renderer=gl', options)

    def process(self):
        process = Mock(pid=4321)
        process.poll.return_value = None
        return process

    def test_missing_or_finished_process_is_not_signalled(self):
        with patch('run_isolated.os.killpg') as kill:
            stop(None)
            process = self.process()
            process.poll.return_value = 0
            stop(process)
        kill.assert_not_called()

    def test_owned_process_gets_graceful_shutdown(self):
        process = self.process()
        with patch('run_isolated.os.killpg') as kill:
            stop(process)
        kill.assert_called_once_with(4321, signal.SIGTERM)
        process.wait.assert_called_once_with(timeout=5)

    def test_timeout_escalates_only_owned_process_group(self):
        process = self.process()
        process.wait.side_effect = [subprocess.TimeoutExpired('owned', 5), 0]
        with patch('run_isolated.os.killpg') as kill:
            stop(process)
        self.assertEqual([call.args for call in kill.call_args_list],
                         [(4321, signal.SIGTERM), (4321, signal.SIGKILL)])

    def test_exit_race_is_harmless(self):
        process = self.process()
        with patch('run_isolated.os.killpg', side_effect=ProcessLookupError):
            stop(process)
        process.wait.assert_not_called()

class PrivateScreen(unittest.TestCase):
    def test_actual_screen_size_is_not_the_requested_size(self):
        self.assertEqual(screen_dimensions('  dimensions:    1920x1008 pixels (508x266 millimeters)'),
                         '1920x1008')

    def test_missing_geometry_cannot_be_reported_as_large_enough(self):
        with self.assertRaisesRegex(RuntimeError, 'actual dimensions'):
            screen_dimensions('server requested 2400x1600, no actual dimensions')

    def test_exact_requested_size_is_read_from_the_server(self):
        self.assertEqual(screen_dimensions('  dimensions:    2400x1600 pixels (610x410 millimeters)'),
                         '2400x1600')
