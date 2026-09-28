import unittest
from compare_link_lighting import require_lighting_parity


class LightingComparison(unittest.TestCase):
    def measurements(self, error):
        return [{'stage': 'links-baseline' if i < 16 else 'links-hover',
                 'rgb_mae_foreground': error} for i in range(88)]

    def test_matching_planar_lighting(self):
        require_lighting_parity(self.measurements(0.55))

    def test_previous_local_viewer_difference_cannot_pass(self):
        with self.assertRaisesRegex(RuntimeError, 'differs'):
            require_lighting_parity(self.measurements(4.07))

    def test_incomplete_matrix_cannot_pass(self):
        with self.assertRaisesRegex(RuntimeError, 'incomplete'):
            require_lighting_parity(self.measurements(0.55)[:-1])

    def test_nan_metric_cannot_pass(self):
        with self.assertRaises(RuntimeError):
            require_lighting_parity(self.measurements(float('nan')))
