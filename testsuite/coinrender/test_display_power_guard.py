import subprocess
import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/coinrender'))
import run_active_window_performance as guard


class DisplayPowerGuardTest(unittest.TestCase):
    def fixture(self, powered_off_after_child=False):
        state = {'enabled': True, 'level': 'Off', 'commands': []}

        def run(command, *args, **kwargs):
            state['commands'].append(command)
            if command[0] == 'xset':
                if command[1] == 'q':
                    text = 'DPMS is ' + ('Enabled' if state['enabled'] else 'Disabled')
                    text += '\nMonitor is ' + state['level'] + '\n'
                    return subprocess.CompletedProcess(command, 0, text, '')
                if command[1] in ('+dpms', '-dpms'):
                    state['enabled'] = command[1] == '+dpms'
                else:
                    state['level'] = command[-1].capitalize()
            elif powered_off_after_child:
                state.update(enabled=True, level='Off')
            return subprocess.CompletedProcess(command, 0, 'synthetic process output', '')

        return state, run

    def test_powered_off_and_unknown_sessions_are_rejected(self):
        guard.require_active('DPMS is Enabled\nMonitor is On')
        guard.require_active('DPMS is Disabled')
        for state in ('Monitor is Off', 'Monitor is Standby', 'Monitor is Suspend', 'unavailable'):
            with self.assertRaises(RuntimeError):
                guard.require_active(state)

    def test_read_only_mode_never_changes_power_or_starts_a_bad_measurement(self):
        state, run = self.fixture()
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(sys, 'argv', ['guard', '--audit-dir', directory]), \
                 patch.object(subprocess, 'run', run), patch.object(guard.performance, 'main') as render:
                with self.assertRaises(RuntimeError):
                    guard.main()
            render.assert_not_called()
            self.assertTrue(all(command == ['xset', 'q'] for command in state['commands']))

    def test_renderer_interruption_restores_original_power_state(self):
        state, run = self.fixture()
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(sys, 'argv', ['guard', '--audit-dir', directory, '--manage-dpms']), \
                 patch.object(subprocess, 'run', run), \
                 patch.object(guard.performance, 'main', side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt):
                    guard.main()
            self.assertTrue(state['enabled'])
            self.assertEqual(state['level'], 'Off')
            self.assertIn('Monitor is Off', (Path(directory) / 'restored.log').read_text())

    def test_power_loss_during_process_excludes_output_and_restores_state(self):
        state, run = self.fixture(powered_off_after_child=True)

        def render():
            subprocess.run(['/usr/bin/time', '-f', '%M', '/frozen/benchmark'])

        with tempfile.TemporaryDirectory() as directory:
            with patch.object(sys, 'argv', ['guard', '--audit-dir', directory, '--manage-dpms']), \
                 patch.object(subprocess, 'run', run), patch.object(guard.performance, 'main', render):
                with self.assertRaises(RuntimeError):
                    guard.main()
            self.assertTrue(state['enabled'])
            self.assertEqual(state['level'], 'Off')
            self.assertIn('synthetic process output', (Path(directory) / 'excluded-process.log').read_text())


if __name__ == '__main__':
    unittest.main()
