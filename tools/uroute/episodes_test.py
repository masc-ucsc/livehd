# SPDX-License-Identifier: BSD-3-Clause
import random
import math
import unittest
from tools.uroute.episodes import ImportanceSampler


class EpisodesTest(unittest.TestCase):
    def test_paper_rms_and_uniform_mixture(self):
        a, b = ('A', 'T', 0), ('B', 'T', 0)
        sampler = ImportanceSampler([a, b], 'paper-rms')
        self.assertEqual(sampler.probabilities(), [.5, .5])
        for value in (0., 4.):
            sampler.observe(a, value)
        for value in (2., 2.):
            sampler.observe(b, value)
        self.assertAlmostEqual(sampler.probabilities()[0], .99 * math.sqrt(8.) / (math.sqrt(8.) + 2.) + .005)
        for _ in range(10):
            sampler.observe(a, 0.)
            sampler.observe(b, 0.)
        self.assertEqual(sampler.probabilities(), [.5, .5])
        for task in (a, b):
            sampler.observe(task, 1e308)
        self.assertEqual(sampler.probabilities(), [.5, .5])
        with self.assertRaisesRegex(ValueError, 'unknown'):
            ImportanceSampler([a], 'invalid')

    def test_importance_and_history_window(self):
        a, b = ("A", "T", 0), ("B", "T", 0)
        sampler = ImportanceSampler([b, a])
        self.assertEqual(sampler.tasks, (a, b))
        self.assertEqual(sampler.probabilities(), [.5, .5])
        sampler.observe(a, 1.)
        sampler.observe(b, 9.)
        expected = 1.01 / 4.02
        self.assertAlmostEqual(sampler.probabilities()[0], expected)
        for _ in range(10):
            sampler.observe(b, 0.)
        self.assertEqual(list(sampler.losses[b]), [0.] * 10)
        self.assertGreater(sampler.probabilities()[1], 0.)
        first, second = random.Random(3), random.Random(3)
        self.assertEqual([sampler.sample(first) for _ in range(20)], [sampler.sample(second) for _ in range(20)])
        with self.assertRaises(ValueError):
            sampler.observe(a, float("nan"))


if __name__ == "__main__":
    unittest.main()
