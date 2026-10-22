"""wgpu/OpenGL uses the requested executor and proves its submitted adapter."""
import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from run import backend_environment, classify, main, wgpu_gl_adapter_evidence


class WgpuOpenGLRunner(unittest.TestCase):
    def trace(self, offscreen=False):
        phase = 'wgpu_offscreen' if offscreen else 'wgpu_surface'
        surface = '' if offscreen else 'surface=1 '
        return ('COIN_RENDER_PHASE ' + phase + ' renderer=opengl vendor_id=0x1002 '
                'device_id=0x0 device_type=Other ' + surface +
                'serial=1 size=256x256 adapter_name="AMD Radeon Graphics (radeonsi, renoir)"\n')

    def test_backend_selection_does_not_inherit_another_executor(self):
        self.assertEqual(backend_environment('wgpu', 'opengl'),
                         {'WGPU_BACKEND': 'gl', 'WGPU_BACKENDS': 'gl'})
        self.assertEqual(backend_environment('wgpu', 'vulkan'),
                         {'WGPU_BACKEND': 'vulkan', 'WGPU_BACKENDS': 'vulkan'})
        self.assertEqual(backend_environment('bgfx', 'opengl'), {})

    def test_submitted_surface_and_offscreen_adapters_are_scoped(self):
        for offscreen in (False, True):
            trace = self.trace(offscreen)
            self.assertTrue(wgpu_gl_adapter_evidence(trace, offscreen)[1])
            self.assertFalse(wgpu_gl_adapter_evidence(trace, not offscreen)[1])
            for wrong in (trace.replace('serial=1', 'serial=0'),
                          trace.replace('AMD Radeon Graphics (radeonsi, renoir)', 'llvmpipe'),
                          trace.replace('device_type=Other', 'device_type=Cpu'),
                          trace.replace('vendor_id=0x1002', 'vendor_id=0x0'),
                          trace.split(' adapter_name=')[0],
                          trace + trace.split(' adapter_name=')[0]):
                with self.subTest(offscreen=offscreen, wrong=wrong):
                    self.assertFalse(wgpu_gl_adapter_evidence(wrong, offscreen)[1])

    def test_opengl_is_admitted_for_an_ordinary_quarter_case(self):
        with tempfile.TemporaryDirectory() as temporary:
            argv = ['run.py', '--harness', str(Path(__file__)), '--artifacts', temporary,
                    '--backend', 'wgpu', '--renderer', 'opengl', '--case', 'first-expose',
                    '--mode', 'object', '--scale', '1', '--require-hardware']
            output = self.trace() + 'RESULT {"status":"PASS"}\n'
            with patch('sys.argv', argv), patch.dict(os.environ, {'DISPLAY': ':99',
                      'WGPU_BACKEND': 'vulkan', 'WGPU_BACKENDS': 'vulkan'}), \
                    patch('run.subprocess.run', return_value=SimpleNamespace(
                        returncode=0, stdout='Accelerated: yes\nAMD Radeon Graphics')), \
                    patch('run.execute', return_value=(0, output)) as execute, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 0)
            env = execute.call_args.args[1]
            self.assertEqual(env['WGPU_BACKEND'], 'gl')
            self.assertEqual(env['WGPU_BACKENDS'], 'gl')
            results = json.loads((Path(temporary) / 'results.json').read_text())
            self.assertEqual(results[0]['status'], 'PASS')
            self.assertTrue(results[0]['hardware_gpu'])
            self.assertTrue(results[0]['backend_gl_contexts'])

    def test_reference_and_fallback_never_become_native_passes(self):
        for record in ('{"status":"REFERENCE_PASS"}',
                       '{"status":"PASS","reference_gl":true}'):
            self.assertEqual(classify(0, self.trace() + 'RESULT ' + record,
                                      backend='wgpu')['status'], 'FAIL')
        self.assertEqual(classify(0, self.trace() + 'switching viewport to Coin/GL\n'
                                 'RESULT {"status":"PASS"}', backend='wgpu')['status'], 'FAIL')
        with patch('sys.argv', ['run.py', '--harness', __file__, '--artifacts', '/unused',
                   '--backend', 'wgpu', '--renderer', 'opengl', '--reference-gl']), \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as stopped:
            main()
        self.assertEqual(stopped.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
