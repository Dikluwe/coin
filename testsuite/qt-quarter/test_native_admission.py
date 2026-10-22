"""A successful final frame cannot hide cold fallback or a lost publication."""
import copy
import json
import unittest

from run import CASES, OFFSCREEN_CASES, classify, native_admission_evidence


class NativeAdmissionGate(unittest.TestCase):
    def setUp(self):
        self.result = {'test': 'native-admission', 'status': 'PASS', 'native_admission': {
            'cold': {'status': 'UNSUPPORTED', 'frames': 0, 'serial': 0,
                     'gl_calls': 0, 'callbacks': 1},
            'first': {'status': 'SUCCESS', 'frames': 1, 'serial': 7},
            'late': {'status': 'UNSUPPORTED', 'frames': 1, 'serial': 7,
                     'gl_calls': 0, 'unchanged_pixels': True},
            'recovered': {'status': 'SUCCESS', 'frames': 2, 'serial': 8,
                          'gl_calls': 0, 'changed_pixels': 100},
            'idle_frames': 0}}

    def classify(self, result):
        return classify(0, 'COIN_RENDER_PHASE bgfx lower_ms=1\nRESULT ' + json.dumps(result),
                        expected_case='native-admission')['status']

    def test_case_uses_native_surface_evidence(self):
        self.assertIn('native-admission', CASES)
        self.assertNotIn('native-admission', OFFSCREEN_CASES)
        self.assertTrue(native_admission_evidence(self.result))
        self.assertEqual(self.classify(self.result), 'PASS')

    def test_final_success_without_transaction_observations_fails(self):
        for result in ({'status': 'PASS'}, {'test': 'first-expose', 'status': 'PASS'},
                       {'test': 'native-admission', 'status': 'PASS'},
                       dict(self.result, native_admission=None)):
            with self.subTest(result=result):
                self.assertEqual(self.classify(result), 'FAIL')

    def test_each_cold_late_and_recovery_observation_is_required(self):
        mutations = [('cold', 'frames', 1), ('cold', 'serial', 7),
                     ('cold', 'status', 'SUCCESS'), ('cold', 'callbacks', 0),
                     ('cold', 'gl_calls', 1), ('late', 'gl_calls', 1),
                     ('late', 'frames', 2), ('late', 'serial', 8),
                     ('late', 'unchanged_pixels', False),
                     ('late', 'status', 'SUCCESS'), ('first', 'serial', 0),
                     ('first', 'frames', 0), ('recovered', 'frames', 1),
                     ('recovered', 'serial', 7), ('recovered', 'changed_pixels', 0),
                     ('recovered', 'status', 'UNSUPPORTED'), ('recovered', 'gl_calls', 1),
                     ('cold', 'frames', False), ('cold', 'callbacks', -1),
                     ('recovered', 'serial', '8')]
        for phase, key, value in mutations:
            with self.subTest(phase=phase, key=key, value=value):
                changed = copy.deepcopy(self.result)
                changed['native_admission'][phase][key] = value
                self.assertFalse(native_admission_evidence(changed))
                self.assertEqual(self.classify(changed), 'FAIL')
        changed = copy.deepcopy(self.result)
        changed['native_admission']['idle_frames'] = 1
        self.assertEqual(self.classify(changed), 'FAIL')

    def test_native_backend_trace_remains_mandatory(self):
        output = 'RESULT ' + json.dumps(self.result)
        self.assertEqual(classify(0, output, expected_case='native-admission')['status'], 'FAIL')
        self.assertEqual(classify(0, 'COIN_RENDER_PHASE bgfx lower_ms=1\n'
                                 'switching viewport to Coin/GL\n' + output,
                                 expected_case='native-admission')['status'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
