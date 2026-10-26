# SPDX-License-Identifier: BSD-3-Clause
"""Hermetic native golden/calibration tests; PyTorch is not a test dependency."""
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

from tools.uroute.export_weights import decode_tensors, encode_tensors
from tools.uroute.features import Maps
from tools.uroute.fixture import attention_cancellation_tensors, cancellation_tensors, inputs, support_cancellation_tensors, tensors
from tools.uroute.maps import read, write

BINARY = str(Path(sys.argv.pop(1)).resolve())
GOLDEN = Path(sys.argv.pop(1))
H2D = str(Path(sys.argv.pop(1)).resolve())
METRICS = str(Path(sys.argv.pop(1)).resolve())


class InferenceTest(unittest.TestCase):
    def test_golden_calibration_and_corruption(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = tensors()
            data = encode_tensors(fixture)
            self.assertEqual(encode_tensors(decode_tensors(data)), data)
            weights = root / "weights.bin"
            weights.write_bytes(data)
            golden = json.loads(GOLDEN.read_text())
            for case in golden["cases"]:
                fixtures = {"cancellation": cancellation_tensors, "support_cancellation": support_cancellation_tensors,
                            "attention_cancellation": attention_cancellation_tensors}
                weights.write_bytes(encode_tensors(fixtures[case["fixture"]]()) if "fixture" in case else data)
                nx, ny, shots = case["shape"][0], case["shape"][1], case["shots"]
                query, support = inputs(nx, ny, shots)
                write(root / "query.bin", query)
                paths = []
                for i, pair in enumerate(support):
                    p = root / f"support{i}.bin"
                    write(p, pair)
                    paths.append(str(p))
                command = [BINARY, "infer", str(weights), str(root / "query.bin"), *paths, str(root / "out.bin")]
                subprocess.run(command, check=True, timeout=10)
                values = read(root / "out.bin").values
                self.assertEqual(len(values), len(case["output"]))
                for actual, expected in zip(values, case["output"]):
                    self.assertLessEqual(abs(actual - expected), 1e-7 + 1e-4 * abs(expected))
                first = (root / "out.bin").read_bytes()
                subprocess.run(command, check=True, timeout=10)
                self.assertEqual(first, (root / "out.bin").read_bytes())
                # Reuse an immutable support pyramid across changed queries,
                # then destroy the caller's maps. The helper compares every
                # value to a fresh forward pass and restores the first query.
                reused_command = [*command]
                reused_command[1] = "infer-reuse"
                subprocess.run(reused_command, check=True, timeout=10)
                self.assertEqual(first, (root / "out.bin").read_bytes())
            weights.write_bytes(data)
            # The native route path must retain RSMT's non-network terms and
            # calibrate absolute utilization=2, DRC=4 from constant support.
            query, _ = inputs(8, 8, 1)
            write(root / "query.bin", query)
            labels = Maps(3, 8, 8, False, [2.] * 64 + [4.] * 64 + [1.] * 64, [(2., 2.), (4., 4.), (1., 1.)])
            write(root / "labels.bin", labels)
            physical = root / "physical.txt"
            physical.write_text('0 0 16 16 1 1 .1 .2 2\n3\n1 2 0 0 0 0 0\n15 14 0 0 0 0 0\n6 0 4 16 1 .9 .9\n1\n2 0 1 2 1 15 14\n')
            subprocess.run([METRICS, str(physical), str(root / "combined"), "uroute", str(weights),
                            str(root / "query.bin"), str(root / "labels.bin")], check=True, timeout=10)
            combined = json.loads((root / "combined.json").read_text())
            self.assertEqual(combined['route_model'], 'uroute')
            self.assertEqual(combined['uroute_support_count'], 1)
            self.assertEqual(combined['uroute_network_grid'], [8, 8])
            for mode in ('base', 'enhanced'):
                self.assertEqual(combined[mode]['measured_nets'], 1)
                self.assertEqual(max(combined[mode]['peak_horizontal'], combined[mode]['peak_vertical']), 2.)
            # Keep the pre-existing geometric estimator invocation working.
            subprocess.run([METRICS, str(physical), str(root / "legacy"), "rsmt"], check=True, timeout=10)
            self.assertEqual(json.loads((root / "legacy.json").read_text())['route_model'], 'rsmt')
            subprocess.run([BINARY, "route-check", str(weights), str(root / "query.bin"), str(root / "labels.bin")],
                           check=True, timeout=10)
            subprocess.run([BINARY, "ladder-check", str(weights), str(root / "query.bin"), str(root / "labels.bin")],
                           check=True, timeout=10)
            subprocess.run([H2D, str(weights), str(root / "query.bin"), str(root / "labels.bin")], check=True, timeout=10)
            # Normalized predictions may extrapolate above the support-label
            # maximum. Clipping here would suppress learned overflow entirely
            # when routed support utilization peaks at or below one.
            fixture["output.weight"] = (fixture["output.weight"][0], bytes(len(fixture["output.weight"][1])))
            fixture["output.bias"] = ((1,), struct.pack("<f", 2.))
            weights.write_bytes(encode_tensors(fixture))
            gradient = [i / 63 for i in range(64)]
            extrapolation = Maps(3, 8, 8, False, gradient + [2*v for v in gradient] + [1.] * 64,
                                 [(0., 1.), (0., 2.), (1., 1.)])
            write(root / "labels.bin", extrapolation)
            subprocess.run([BINARY, "route-check", str(weights), str(root / "query.bin"), str(root / "labels.bin")],
                           check=True, timeout=10)
            # Truncation, schema mismatch and unknown versions must fail.
            for bad in (data[:-1], data[:8] + b"\xff\xff\xff\xff" + data[12:], encode_tensors({"wrong": ((1,), b"\0" * 4)})):
                weights.write_bytes(bad)
                result = subprocess.run(command, capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"URoute", result.stderr)


if __name__ == "__main__":
    unittest.main()
