import sys
from pathlib import Path
import tempfile
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/coinrender'))
import run_p20_physical_matrix as matrix


class PhysicalMatrixEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.left = Path(self.directory.name)/'left.ppm'
        self.right = Path(self.directory.name)/'right.ppm'

    def write(self, path, data):
        path.write_bytes(b'P6\n128 128\n255\n' + data)

    def test_identical_images_are_exact(self):
        pixels = bytes([25,50,75])*128*128
        self.write(self.left, pixels); self.write(self.right, pixels)
        self.assertEqual(matrix.rgb_metrics(self.left,self.right),
                         dict(mae=0,max=0,pixels_over3=0))

    def test_sparse_large_error_is_not_hidden_by_mean(self):
        self.write(self.left,bytes(128*128*3))
        pixels = bytearray(128*128*3); pixels[101] = 255
        self.write(self.right,pixels)
        metrics = matrix.rgb_metrics(self.left,self.right)
        self.assertLess(metrics['mae'],1)
        self.assertEqual(metrics['max'],255)
        self.assertEqual(metrics['pixels_over3'],1)

    def test_truncated_and_wrong_size_images_are_rejected(self):
        self.write(self.left,bytes(128*128*3))
        for data in (b'P6\n128 128\n255\n'+bytes(128*128*3-1),
                     b'P6\n64 256\n255\n'+bytes(128*128*3)):
            self.right.write_bytes(data)
            with self.assertRaises(ValueError): matrix.rgb_metrics(self.left,self.right)

    def test_orientation_difference_is_reported_without_correction(self):
        row = bytes([200,50,10])*128
        black = bytes(128*3*127)
        self.write(self.left,row+black); self.write(self.right,black+row)
        metrics = matrix.rgb_metrics(self.left,self.right)
        self.assertEqual(metrics['pixels_over3'],256)
        self.assertEqual(metrics['max'],200)


if __name__ == '__main__': unittest.main()
