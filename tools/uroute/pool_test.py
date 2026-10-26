# SPDX-License-Identifier: BSD-3-Clause
"""Exercise the real C++ data reader against the offline writer's output."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.uroute.export_weights import encode_tensors
from tools.uroute.features import Maps, features
from tools.uroute.fixture import tensors
from tools.uroute.maps import encode, write
from tools.uroute.pool import canonical, publish_sample, publish_weights, read_manifest

BINARY = str(Path(sys.argv.pop(1)).resolve())
AREA = 256 * 256


def feature(value):
    return Maps(4, 256, 256, True, [value] * (4 * AREA), [(0., 1.)] * 4)


def labels():
    return Maps(3, 256, 256, False, [.5] * AREA + [0.] * AREA + [1.] * AREA,
                [(.5, .5), (0., 0.), (1., 1.)])


def metadata(top, tech, created):
    return {"top": top, "technology": tech, "created_at": created, "source": "external", "status": "routed",
            "seed": 0, "floorplan": {"die_width": 16., "die_height": 16., "utilization": .5, "macro_signature": "macro"}}


class PoolTest(unittest.TestCase):
    def test_selection_hashes_and_atomic_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "data"
            query = root / "query.bin"
            write(query, feature(0.))

            def select(top="A", tech="T", shots=2, minimum=1, fallback="nearest"):
                result = subprocess.run([BINARY, str(query), str(directory), top, tech, str(shots), str(minimum), fallback, "macro"],
                                        check=True, capture_output=True, text=True, timeout=10)
                return json.loads(result.stdout)

            self.assertIn("missing", select()["skipped"])
            original = dict(metadata("A", "T", 1), routed_def_sha256="a" * 64, controls_sha256="1" * 64)
            aold = publish_sample(directory, original, feature(.2), labels())
            self.assertEqual(publish_sample(directory, original, feature(.2), labels())["id"], aold["id"])
            repeated = dict(original, created_at=100)
            self.assertEqual(publish_sample(directory, repeated, feature(.2), labels()), aold)
            anew = publish_sample(directory, metadata("A", "T", 2), feature(.8), labels())
            b = publish_sample(directory, metadata("B", "T", 3), feature(.1), labels())
            c = publish_sample(directory, metadata("C", "other", 4), feature(0.), labels())
            self.assertEqual(len(read_manifest(directory)["samples"]), 4)
            self.assertIn("weights", select()["skipped"])
            weights = encode_tensors(tensors())
            publish_weights(directory, weights, trained_on="test-fixture", provenance={"source": "synthetic-test-only"})
            enriched = publish_sample(directory, dict(original, verification="additional evidence"), feature(.2), labels(),
                                      extra_files={"verification.json": b'{"checked":true}\n'})
            self.assertEqual(enriched["id"], aold["id"])
            self.assertEqual(len(read_manifest(directory)["samples"]), 4)
            self.assertEqual(len(read_manifest(directory)["observations"]), 1)
            # A different source QoR/provenance digest in the controls file
            # cannot turn identical routed geometry and maps into a new shot.
            old_sample = (directory / aold['directory'] / 'sample.json').read_bytes()
            provenance_only = publish_sample(directory, dict(original, controls_sha256="2" * 64), feature(.2), labels())
            self.assertEqual(provenance_only['id'], aold['id'])
            self.assertEqual(provenance_only['created_at'], aold['created_at'])
            self.assertEqual(len(read_manifest(directory)["samples"]), 4)
            self.assertEqual(len(read_manifest(directory)["observations"]), 2)
            self.assertEqual((directory / aold['directory'] / 'sample.json').read_bytes(), old_sample)
            same = select()
            self.assertTrue(same["usable"], same)
            self.assertEqual(same["sample_ids"], [anew["id"], aold["id"]])
            self.assertEqual(same, select())
            nearest = select(top="unseen")
            self.assertEqual(nearest["sample_ids"], [b["id"], aold["id"]])
            other_tech = select(top="unseen", tech="other", shots=1)
            self.assertEqual(other_tech["sample_ids"], [c["id"]])
            self.assertEqual(select(minimum=3, shots=3)["sample_ids"], [b["id"], aold["id"], anew["id"]])
            self.assertIn("same-design", select(top="unseen", fallback="skip")["skipped"])
            self.assertIn("exceeds", select(shots=5, fallback="skip")["skipped"])
            # Publication must retain an unrelated writer's samples and every
            # failed route. Failed samples can never become support pairs.
            failed = metadata("A", "T", 5)
            failed["status"] = "unrouted"
            publish_sample(directory, failed, feature(.3), last_global_overflow=Maps(1, 1, 1, False, [2.], [(2., 2.)]))
            unavailable = metadata("A", "T", 6)
            unavailable["status"] = "unrouted"
            absent = publish_sample(directory, unavailable, feature(.4), overflow_unavailable="pin access failed before GRT")
            self.assertEqual(absent["last_global_overflow_status"], "unavailable")
            self.assertFalse((directory / absent["directory"] / "last_global_overflow.bin").exists())
            self.assertEqual(len(read_manifest(directory)["samples"]), 6)
            self.assertEqual(select()["sample_ids"], same["sample_ids"])
            self.assertNotEqual(select()["cache_identity"], same["cache_identity"])
            self.assertEqual(len(list(directory.glob("weights_*.bin"))), 1)
            manifest_path = directory / "manifest.json"
            good = manifest_path.read_bytes()
            manifest = json.loads(good)
            # Incompatible sample versions are ignored; incompatible manifest
            # versions refuse the whole directory with a clear skip reason.
            manifest["samples"][1]["feature_version"] = 999
            manifest_path.write_bytes(canonical(manifest))
            self.assertEqual(select()["sample_ids"], [aold["id"]])
            manifest["arch_version"] = 999
            manifest_path.write_bytes(canonical(manifest))
            self.assertIn("version", select()["skipped"])
            manifest_path.write_bytes(good[:-2] + b',"arch_version":1}\n')
            self.assertIn("duplicate", select()["skipped"])
            manifest_path.write_bytes(good)
            # Corruption is not admitted under an unchanged expected digest.
            path = directory / aold["directory"] / "features.bin"
            path.write_bytes(path.read_bytes()[:-1])
            self.assertIn("digest", select()["skipped"])

    def test_export_and_geometry_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run([BINARY, "export", str(root)], check=True, timeout=10)
            data = json.loads((root / "metadata.json").read_text())
            self.assertEqual(data["route_grid"], [4, 4])
            self.assertEqual(data["network_grid"], [256, 256])
            self.assertEqual(data["dbu_per_micron"], 1000)
            self.assertEqual(data["features_sha256"], hashlib.sha256((root / "features.bin").read_bytes()).hexdigest())
            design = {"core": [0, 0, 16, 16], "nodes": [
                {"x": 1, "y": 2, "width": 1, "height": 1},
                {"x": 15, "y": 14, "width": 0, "height": 0},
                {"x": 6, "y": 0, "width": 4, "height": 16, "macro": True}],
                      "nets": [{"pins": [[1, 2], [15, 14]]}]}
            self.assertEqual((root / "features.bin").read_bytes(), encode(features(design, 4, 4)))
            self.assertEqual(len(data["placement_identity"]), 64)
            first = (root / "metadata.json").read_bytes()
            subprocess.run([BINARY, "export", str(root)], check=True, timeout=10)
            self.assertEqual(first, (root / "metadata.json").read_bytes())
            self.assertFalse(list(root.glob("*.tmp")))

    def test_bad_offline_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            bad = metadata("A", "T", 1)
            bad["status"] = "unrouted"
            with self.assertRaises(ValueError):
                publish_sample(temporary, bad, feature(0.))
            bad = metadata("A", "T", 1)
            bad["floorplan"]["utilization"] = 2
            with self.assertRaises(ValueError):
                publish_sample(temporary, bad, feature(0.), labels())


if __name__ == "__main__":
    unittest.main()
