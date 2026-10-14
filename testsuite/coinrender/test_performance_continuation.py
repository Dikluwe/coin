import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/coinrender'))
import run_performance_continuation as performance


class PerformanceProtocolTest(unittest.TestCase):
    def setUp(self):
        performance.configure_variants()

    def test_ab_preserves_backend_and_output_scope(self):
        for backend in ('bgfx-vulkan', 'bgfx-opengl', 'wgpu-vulkan'):
            original = performance.animation.VARIANTS[backend]
            for choice in ('literal', 'reserve'):
                self.assertEqual(performance.animation.VARIANTS[backend + '-' + choice], original)

    def test_measurement_removes_intrusive_flags_and_records_one_ab_choice(self):
        with patch.dict('os.environ', {
            'COIN_RENDER_TRACE_PHASES': '1', 'COIN_BGFX_TRACE_GL_ADAPTER': '1',
            'COIN_WGPU_GPU_TIMESTAMPS': '1', 'COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE': '1',
            'COIN_RENDER_DISABLE_CUBE_OVERLAY': '1', 'VK_DRIVER_FILES': 'wrong-device',
        }, clear=True):
            on = performance.environment(Path('/frozen/bgfx'), 'bgfx-opengl-reserve', 'nvidia')
            off = performance.environment(Path('/frozen/bgfx'), 'bgfx-opengl-literal', 'nvidia')
        for key in ('COIN_RENDER_TRACE_PHASES', 'COIN_BGFX_TRACE_GL_ADAPTER',
                    'COIN_WGPU_GPU_TIMESTAMPS', 'COIN_RENDER_DISABLE_CUBE_OVERLAY'):
            self.assertNotIn(key, on)
            self.assertNotIn(key, off)
        self.assertEqual(on['COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE'], '0')
        self.assertEqual(off['COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE'], '1')
        self.assertEqual(on['COIN_BGFX_RENDERER'], 'opengl')
        self.assertEqual(on['VK_DRIVER_FILES'], '/usr/share/vulkan/icd.d/nvidia_icd.json')
        self.assertEqual(on['__EGL_VENDOR_LIBRARY_FILENAMES'], '/usr/share/glvnd/egl_vendor.d/10_nvidia.json')
        self.assertEqual(on['LD_LIBRARY_PATH'], '/frozen/bgfx/lib')
        self.assertEqual({k:v for k,v in on.items() if k != 'COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE'},
                         {k:v for k,v in off.items() if k != 'COIN_RENDER_DISABLE_OBJECT_UPDATE_RESERVE'})

    def test_geometry_full_and_partial_use_identical_deterministic_animation(self):
        self.assertEqual(performance.animation.case_options('geometry-10'), ('geometry', 10))
        self.assertEqual(performance.animation.case_options('geometry-100'), ('geometry', 100))
        values = list(range(1,121))
        stats = performance.animation.sample_stats(values)
        self.assertEqual(stats['median_ms'], 60.5)
        self.assertEqual(stats['p95_ms'], 114)
        self.assertEqual(stats['p99_ms'], 119)


if __name__ == '__main__':
    unittest.main()
