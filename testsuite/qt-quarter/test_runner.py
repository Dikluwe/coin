"""CPU-only tests for the anti-false-positive gate."""
import unittest
import os
import sys
from run import classify, execute

class ResultGate(unittest.TestCase):
    def test_fallback_is_failure(self):
        self.assertEqual(classify(0, 'RESULT {"status":"PASS"}')['status'], 'FAIL')

    def test_initialization_is_not_submission(self):
        self.assertEqual(classify(0, 'COIN_WGPU_PHASE bgfx_device\nRESULT {"status":"PASS"}')['status'], 'FAIL')

    def test_real_submission(self):
        self.assertEqual(classify(0, 'COIN_WGPU_PHASE bgfx lower_ms=1\nRESULT {"status":"PASS"}')['status'], 'PASS')

    def test_distinct_nonpasses(self):
        for code, status in [(77, 'SKIP'), (78, 'UNSUPPORTED'), (1, 'FAIL')]:
            self.assertEqual(classify(code, 'RESULT {"status":"' + status + '"}')['status'], status)

    def test_crash_cannot_pass(self):
        self.assertEqual(classify(-11, 'RESULT {"status":"PASS"}')['status'], 'FAIL')

    def test_malformed_cannot_pass(self):
        self.assertEqual(classify(0, 'RESULT invalid')['status'], 'FAIL')

    def test_timeout_kills_test_process(self):
        code, output = execute([sys.executable, '-c', 'import time; time.sleep(5)'], os.environ, .1)
        self.assertLess(code, 0)
        self.assertIn('HARNESS_TIMEOUT', output)

if __name__ == '__main__':
    unittest.main()
