"""CPU-only tests for the anti-false-positive gate."""
import unittest
import os
import sys
from types import SimpleNamespace
from unittest.mock import patch
from run import classify, execute, window_manager_available, parse_lock_state, select_variants, session_locked, device_evidence, bgfx_gl_adapter_evidence

class ResultGate(unittest.TestCase):
    def test_backend_context_proof_rejects_cpu_unknown_and_absent(self):
        def trace(renderer):
            return 'COIN_RENDER_PHASE bgfx_gl_adapter vendor=Mesa renderer='+renderer+' version=4.6\n'
        self.assertTrue(bgfx_gl_adapter_evidence(trace('AMD Radeon Graphics'))[1])
        self.assertTrue(bgfx_gl_adapter_evidence(trace('NVIDIA GeForce RTX 3060'))[1])
        self.assertFalse(bgfx_gl_adapter_evidence(trace('llvmpipe'))[1])
        self.assertFalse(bgfx_gl_adapter_evidence(trace('Unknown'))[1])
        self.assertFalse(bgfx_gl_adapter_evidence('')[1])
        self.assertFalse(bgfx_gl_adapter_evidence(trace('AMD Radeon Graphics')+trace('llvmpipe'))[1])

    def test_offscreen_proof_is_scoped_and_requires_publication_serial(self):
        trace = ('COIN_RENDER_PHASE wgpu_offscreen renderer=vulkan vendor_id=0x1002 '
                 'device_id=0x1638 device_type=IntegratedGpu serial=3 size=256x256\n')
        result = 'RESULT {"status":"PASS"}'
        self.assertEqual(classify(0, trace + result, backend='wgpu')['status'], 'FAIL')
        self.assertEqual(classify(0, trace + result, backend='wgpu', offscreen=True)['status'], 'PASS')
        self.assertEqual(classify(0, trace.replace('serial=3', 'serial=0') + result,
                                  backend='wgpu', offscreen=True)['status'], 'FAIL')
        self.assertEqual(device_evidence(trace, 'wgpu'), [])
        self.assertEqual(device_evidence(trace, 'wgpu', True)[0], ('vulkan','0x1002','0x1638'))

    def test_renderer_names_keep_exact_identity(self):
        def trace(renderer):
            return ('COIN_RENDER_PHASE bgfx_device renderer=' + renderer +
                    ' vendor_id=0x0000 device_id=0x0000 homogeneous_depth=1')
        self.assertEqual(device_evidence(trace('OpenGL 4.3'), 'bgfx'),
                         [('opengl', '0x0000', '0x0000')])
        self.assertEqual(device_evidence(trace('Vulkan'), 'bgfx')[0][0], 'vulkan')
        self.assertEqual(device_evidence(trace('Unknown API'), 'bgfx')[0][0], 'Unknown API')
        self.assertEqual(device_evidence(trace('OpenGL 4.3'), 'wgpu'), [])

    def test_targeted_retry_preserves_default_matrix(self):
        variants = [(m, 'opaque', s) for m in ('object', 'weighted_oit') for s in (1, 2)]
        self.assertEqual(select_variants(variants), variants)
        self.assertEqual(select_variants(variants, ['object'], [2]),
                         [('object', 'opaque', 2)])
        with self.assertRaisesRegex(ValueError, 'no test variants'):
            select_variants(variants[:1], scales=[2])

    def test_lock_probe_does_not_activate_services(self):
        with patch('run.subprocess.run', return_value=SimpleNamespace(
                returncode=0, stdout='boolean false')) as probe:
            self.assertIsNone(session_locked())
        self.assertEqual(probe.call_count, 3)
        self.assertTrue(all('org.freedesktop.DBus.NameHasOwner' in call.args[0]
                            for call in probe.call_args_list))

    def test_lock_probe_reads_existing_service(self):
        with patch('run.subprocess.run', side_effect=[
                SimpleNamespace(returncode=0, stdout='boolean true'),
                SimpleNamespace(returncode=0, stdout='boolean true')]) as probe:
            self.assertTrue(session_locked())
        self.assertEqual(probe.call_count, 2)
        self.assertIn('org.cinnamon.ScreenSaver.GetActive', probe.call_args.args[0])

    def test_lock_probe_is_explicit(self):
        self.assertIs(parse_lock_state('method return\n   boolean true\n'), True)
        self.assertIs(parse_lock_state('method return\n   boolean false\n'), False)
        self.assertIsNone(parse_lock_state('service unavailable'))

    def test_fallback_is_failure(self):
        self.assertEqual(classify(0, 'RESULT {"status":"PASS"}')['status'], 'FAIL')

    def test_initialization_is_not_submission(self):
        self.assertEqual(classify(0, 'COIN_RENDER_PHASE bgfx_device\nRESULT {"status":"PASS"}')['status'], 'FAIL')

    def test_real_submission(self):
        self.assertEqual(classify(0, 'COIN_RENDER_PHASE bgfx lower_ms=1\nRESULT {"status":"PASS"}')['status'], 'PASS')

    def test_explicit_gl_reference_does_not_weaken_bgfx_gate(self):
        result = 'RESULT {"status":"PASS", "reference_gl":true}'
        self.assertEqual(classify(0, result)['status'], 'FAIL')
        self.assertEqual(classify(0, 'COIN_RENDER_PHASE bgfx lower_ms=1\n' + result)['status'], 'FAIL')
        self.assertEqual(classify(0, 'RESULT {"status":"REFERENCE_PASS"}')['status'], 'FAIL')
        self.assertEqual(classify(0, result, require_bgfx=False)['status'], 'PASS')
        self.assertEqual(classify(-11, result, require_bgfx=False)['status'], 'FAIL')

    def test_wgpu_native_submission_and_backend_identity(self):
        trace = ('COIN_RENDER_PHASE wgpu_surface renderer=vulkan vendor_id=0x1002 '
                 'device_id=0x1636 device_type=IntegratedGpu surface=1 serial=3 size=640x480\n')
        result = 'RESULT {"status":"PASS"}'
        self.assertEqual(classify(0, trace + result, backend='wgpu')['status'], 'PASS')
        self.assertEqual(classify(0, trace + result)['status'], 'FAIL')
        self.assertEqual(classify(0, 'COIN_RENDER_PHASE bgfx lower_ms=1\n' + result,
                                  backend='wgpu')['status'], 'FAIL')
        self.assertEqual(classify(0, trace.replace('serial=3', 'serial=0') + result,
                                  backend='wgpu')['status'], 'FAIL')
        self.assertEqual(classify(0, trace + 'switching viewport to Coin/GL\n' + result,
                                  backend='wgpu')['status'], 'FAIL')
        self.assertEqual(classify(0, trace + 'RESULT {"status":"REFERENCE_PASS"}',
                                  backend='wgpu')['status'], 'FAIL')

    def test_distinct_nonpasses(self):
        for code, status in [(77, 'SKIP'), (78, 'UNSUPPORTED'), (1, 'FAIL')]:
            self.assertEqual(classify(code, 'RESULT {"status":"' + status + '"}')['status'], status)

    def test_crash_cannot_pass(self):
        self.assertEqual(classify(-11, 'RESULT {"status":"PASS"}')['status'], 'FAIL')

    def test_malformed_cannot_pass(self):
        self.assertEqual(classify(0, 'RESULT invalid')['status'], 'FAIL')

    def test_window_manager_probe_is_explicit(self):
        self.assertTrue(window_manager_available(
            '_NET_SUPPORTING_WM_CHECK(WINDOW): window id # 0x400001'))
        self.assertFalse(window_manager_available(
            '_NET_SUPPORTING_WM_CHECK: no such atom on any window.'))
        self.assertFalse(window_manager_available(
            '_NET_SUPPORTING_WM_CHECK(WINDOW): window id # 0x0'))

    def test_timeout_kills_test_process(self):
        code, output = execute([sys.executable, '-c', 'import time; time.sleep(5)'], os.environ, .1)
        self.assertLess(code, 0)
        self.assertIn('HARNESS_TIMEOUT', output)

if __name__ == '__main__':
    unittest.main()
