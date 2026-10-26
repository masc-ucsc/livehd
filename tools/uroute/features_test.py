# SPDX-License-Identifier: BSD-3-Clause
"""Byte parity, analytical invariants and corrupt-map rejection."""
import copy
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from tools.uroute.features import Maps, features, resample
from tools.uroute.maps import decode, encode

BINARY = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else None


class FeaturesTest(unittest.TestCase):
    def test_resample_conserves_area(self):
        values = [0., 2., 7., 1., 3., 5.]
        for nx, ny in [(1, 1), (7, 5), (256, 256), (3, 2)]:
            result = resample(values, 3, 2, nx, ny)
            self.assertAlmostEqual(sum(result) / len(result), sum(values) / len(values), places=11)
        self.assertEqual(resample(values, 3, 2, 3, 2), values)
        with self.assertRaises(ValueError):
            resample([math.nan], 1, 1, 2, 2)

    def test_analytic_raw_maps(self):
        # Bbox demand = 1/2 + 1/4; half the four tiles are covered.
        d = {"core": [0., 0., 4., 4.], "nodes": [
            {"x": 2., "y": 0., "width": 2., "height": 2., "macro": True},
            {"x": 0., "y": 0., "width": 1., "height": 1.}],
             "nets": [{"pins": [[0., 0.], [2., 4.]]}]}
        m = features(d, 2, 2, 2, 2)
        self.assertEqual(m.ranges, [(0., .75), (0., .75), (0., 1.), (0., 1.)])
        self.assertEqual(m.values, [1., 1., 0., 0., 1., 0., 0., 1., 0., 0., 1., 0., 1., 0., 0., 0.])
        one = features(d, 1, 1, 1, 1)
        self.assertEqual(one.values, [0.] * 4)
        self.assertEqual(one.ranges, [(.375, .375), (1.5, 1.5), (1., 1.), (1., 1.)])

    def test_cpp_byte_parity(self):
        self.assertIsNotNone(BINARY)
        d = {"core": [10., 20., 50., 44.], "nodes": [
            {"x": 11.25, "y": 20.5, "width": 3.5, "height": 2.25},
            {"x": 18.1, "y": 23.2, "width": 11.3, "height": 9.4, "macro": True},
            {"x": 19., "y": 24., "width": 5., "height": 5., "macro": True},
            {"x": 30., "y": 25., "width": 8., "height": 7., "macro": True, "mask": True},
            {"x": 38., "y": 40., "width": 5., "height": 4., "pad_x": 1., "pad_y": .5},
            {"x": 15., "y": 28., "width": 5., "height": 5., "mask": True}],
             "nets": [
                 {"pins": [[8., 18.], [28., 40.], [16., 25.], [20., 33.]], "weight": 999},
                 {"pins": [[12., 22.], [48., 42.]]},
                 {"pins": [[15., 30.], [35., 30.]]},  # zero-area box
                 {"pins": [[10., 20.], [50., 44.]]},  # boundary IOs
                 {"pins": [[12., 23.], [49., 43.]], "enabled": False}]}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "geometry.json"
            path.write_text(json.dumps(d))
            for nx, ny, ox, oy in [(1, 1, 1, 1), (3, 7, 5, 4), (7, 5, 3, 2), (64, 64, 256, 256)]:
                expected = encode(features(d, nx, ny, ox, oy))
                out = root / "features.bin"
                subprocess.run([BINARY, "features", str(path), str(out), *map(str, (nx, ny, ox, oy))], check=True, timeout=10)
                actual = out.read_bytes()
                self.assertEqual(actual, expected, (nx, ny, ox, oy))
                self.assertEqual(encode(decode(actual)), actual)
        # Placement-only masks and net weights cannot affect physical features.
        changed = copy.deepcopy(d)
        changed["nodes"].pop(3)
        changed["nets"][0]["weight"] = .001
        self.assertEqual(encode(features(d, 8, 8)), encode(features(changed, 8, 8)))

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
        raw = Maps(1, 2, 1, False, [.1, .9], [(.1, .9)])
        self.assertEqual(encode(decode(encode(raw))), encode(raw))


if __name__ == "__main__":
    unittest.main()
