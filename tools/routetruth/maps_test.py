# SPDX-License-Identifier: BSD-3-Clause
"""Area-conserving resampling, map file round trip and corrupt-file rejection."""
import math
import struct
import unittest

from tools.routetruth.maps import Maps, decode, encode, raw, resample


class MapsTest(unittest.TestCase):
    def test_resample_conserves_area(self):
        values = [0., 2., 7., 1., 3., 5.]
        for nx, ny in [(1, 1), (7, 5), (256, 256), (3, 2)]:
            result = resample(values, 3, 2, nx, ny)
            self.assertAlmostEqual(sum(result) / len(result), sum(values) / len(values), places=11)
        self.assertEqual(resample(values, 3, 2, 3, 2), values)
        with self.assertRaises(ValueError):
            resample([math.nan], 1, 1, 2, 2)

    def test_format_is_stable(self):
        # The on-disk layout of existing label files: magic, version, shape, ranges, f32 payload.
        data = encode(raw([[.5, 2.], [0., 0.]], 2, 1))
        self.assertEqual(data[:8], b"UROUTEM1")
        self.assertEqual(struct.unpack_from("<5I", data, 8), (1, 2, 2, 1, 0))
        self.assertEqual(struct.unpack_from("<4d", data, 28), (.5, 2., 0., 0.))
        self.assertEqual(struct.unpack_from("<4f", data, 60), (.5, 2., 0., 0.))
        self.assertEqual(len(data), 76)

    def test_reject_corrupt_maps(self):
        m = Maps(1, 2, 1, True, [0., 1.], [(0., 7.)])
        valid = encode(m)
        for bad in [valid[:2], valid[:-1], valid + b"x", b"badmagic" + valid[8:],
                    valid[:8] + struct.pack("<I", 999) + valid[12:],
                    valid[:24] + struct.pack("<I", 2) + valid[28:],
                    valid[:-4] + struct.pack("<f", math.nan),
                    valid[:-4] + struct.pack("<f", 2.)]:
            with self.assertRaises(ValueError):
                decode(bad)
        unnormalized = Maps(1, 2, 1, False, [.1, .9], [(.1, .9)])
        self.assertEqual(encode(decode(encode(unnormalized))), encode(unnormalized))


if __name__ == "__main__":
    unittest.main()
