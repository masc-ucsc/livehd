# SPDX-License-Identifier: BSD-3-Clause
import unittest
from tools.uroute.score import metrics, ranks


class ScoreTest(unittest.TestCase):
    def test_identity_and_reverse(self):
        values = list(map(float, range(100)))
        same = metrics(values, values, 10, 10)
        for key in ("pearson", "spearman", "ssim", "hotspot_top5_overlap"):
            self.assertAlmostEqual(same[key], 1.)
        self.assertEqual(same["nrms"], 0.)
        reverse = metrics(values, values[::-1], 10, 10)
        self.assertAlmostEqual(reverse["pearson"], -1.)
        self.assertAlmostEqual(reverse["spearman"], -1.)
        self.assertEqual(reverse["hotspot_top5_overlap"], 0.)

    def test_ties_and_constant_maps(self):
        self.assertEqual(ranks([3, 1, 1, 5]), [2, .5, .5, 3])
        flat = metrics([0.] * 100, [0.] * 100, 10, 10)
        self.assertIsNone(flat["pearson"])
        self.assertIsNone(flat["spearman"])
        self.assertEqual(flat["nrms"], 0.)
        self.assertAlmostEqual(flat["ssim"], 1.)
        self.assertAlmostEqual(flat["hotspot_top5_overlap"], .05)
        wrong = metrics([0.] * 100, [1.] * 100, 10, 10)
        self.assertIsNone(wrong["nrms"])
        self.assertEqual(wrong["rmse"], 1.)
        self.assertLess(wrong["ssim"], .001)

    def test_shape_errors(self):
        with self.assertRaises(ValueError):
            metrics([0.], [0., 1.], 1, 1)
        with self.assertRaises(ValueError):
            metrics([0.], [float("nan")], 1, 1)


if __name__ == "__main__":
    unittest.main()
