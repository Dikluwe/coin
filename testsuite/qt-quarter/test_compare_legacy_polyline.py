import unittest
from unittest.mock import patch
from pathlib import Path
import numpy as np
from PIL import Image
import compare_freecad_legacy_polyline as comparison


class PolylineComparisonTest(unittest.TestCase):
    def images(self, missing=False):
        values = {}
        for directory in ('oracle', 'candidate'):
            for stage in ('polyline-off', 'polyline-first', 'polyline-updated',
                          'polyline-cancelled'):
                data = np.zeros((300, 400, 3), dtype=np.uint8)
                if stage in ('polyline-first', 'polyline-updated'):
                    if directory == 'oracle' or not missing:
                        data[60:62, 40:180, 2] = 255
                values[str(Path(directory) / (stage + '.png'))] = Image.fromarray(data)
        return values

    def test_matching_line(self):
        values = self.images()
        with patch.object(comparison.Image, 'open', side_effect=lambda p: values[str(p)]):
            result = comparison.compare(Path('oracle'), Path('candidate'))
        self.assertEqual(result['polyline-first']['affected_mae'], 0)

    def test_missing_thin_line_is_not_hidden_by_background(self):
        values = self.images(missing=True)
        with patch.object(comparison.Image, 'open', side_effect=lambda p: values[str(p)]):
            with self.assertRaisesRegex(ValueError, 'contribution differs'):
                comparison.compare(Path('oracle'), Path('candidate'))


if __name__ == '__main__':
    unittest.main()
