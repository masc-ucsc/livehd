import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from channel_ir import OutputConflict, check, main


class ChannelIrTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.analysis = {"liberty_sha256": "a" * 64, "sdc_sha256": "b" * 64,
                         "corner": "BC", "source_model": "FULL", "power_model": "matched default activity",
                         "voltages_v": {"VDD": "0.7", "VSS": "0"}}
        self.manifest = {}
        for arm in ("reference", "candidate"):
            report = {"profile": "pg_asap7", "reference_revision": "same-revision",
                      "channels": [] if arm == "reference" else [{"style": "skip"}],
                      "channel_max_ir_drop_v": "0.001"}
            (self.root / (arm + ".json")).write_text(json.dumps(report))
            rails = {}
            for rail, voltage in (("VDD", ".699"), ("VSS", ".001")):
                name = f"{arm}_{rail}.csv"
                self.csv(name, rail, voltage)
                rails[rail] = name
            self.manifest[arm] = {"analysis": copy.deepcopy(self.analysis),
                                  "power_grid_report": arm + ".json", "rails": rails}

    def csv(self, name, rail, voltage, x="1.0"):
        (self.root / name).write_text(
            "Instance,Terminal,Layer,X location,Y location,Voltage\n"
            f"cell,{rail},M1,{x},2.0,{voltage}\n")

    def evaluate(self):
        return check(self.manifest, self.root)

    def test_exact_limit_and_both_rails(self):
        self.csv("candidate_VDD.csv", "VDD", ".698")
        self.csv("candidate_VSS.csv", "VSS", ".002")
        self.assertTrue(self.evaluate()["channel_ir_delta_within_limit"])
        self.csv("candidate_VSS.csv", "VSS", ".002001")
        result = self.evaluate()
        self.assertFalse(result["channel_ir_delta_within_limit"])
        self.assertEqual(result["rails"]["VSS"]["max_terminal_drop_increase_v"], "0.001001")

    def test_peak_movement_cannot_hide_terminal_degradation(self):
        for arm, values in (("reference", (".690", ".699")), ("candidate", (".699", ".690"))):
            self.csv(f"{arm}_VDD.csv", "VDD", values[0])
            with (self.root / f"{arm}_VDD.csv").open("a") as stream:
                stream.write(f"other,VDD,M1,3,4,{values[1]}\n")
        result = self.evaluate()
        self.assertFalse(result["channel_ir_delta_within_limit"])
        self.assertEqual(result["rails"]["VDD"]["worst_drop_increase_v"], "0")

    def test_context_and_placement_must_match(self):
        self.manifest["candidate"]["analysis"]["corner"] = "WC"
        with self.assertRaisesRegex(ValueError, "contexts differ"):
            self.evaluate()
        self.manifest["candidate"]["analysis"] = copy.deepcopy(self.analysis)
        self.csv("candidate_VDD.csv", "VDD", ".699", x="1.1")
        with self.assertRaisesRegex(ValueError, "placement differ"):
            self.evaluate()

    def test_invalid_or_missing_budget(self):
        path = self.root / "candidate.json"
        report = json.loads(path.read_text())
        for limit in (None, "NaN", "Infinity", "-0.1"):
            report["channel_max_ir_drop_v"] = limit
            path.write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                self.evaluate()

    def test_bad_voltage_data_is_not_acceptance(self):
        for value in ("NaN", "Infinity", ""):
            self.csv("candidate_VDD.csv", "VDD", value)
            with self.assertRaises(ValueError):
                self.evaluate()
        self.csv("candidate_VDD.csv", "VDD", ".699")
        with (self.root / "candidate_VDD.csv").open("a") as stream:
            stream.write("cell,VDD,M1,1,2,.699\n")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.evaluate()

    def test_cli_failure_replaces_stale_success(self):
        manifest = self.root / "broken.json"
        output = self.root / "verdict.json"
        manifest.write_text("{")
        output.write_text('{"channel_ir_delta_within_limit": true}')
        with patch("sys.argv", ["channel_ir", str(manifest), "--output", str(output)]):
            self.assertEqual(main(), 1)
        self.assertFalse(json.loads(output.read_text())["channel_ir_delta_within_limit"])

    def test_cli_does_not_overwrite_aliased_evidence(self):
        manifest = self.root / "manifest.json"
        manifest.write_text(json.dumps(self.manifest))
        output = self.root / "verdict.json"
        for source in (manifest, self.root / "candidate_VDD.csv"):
            original = source.read_bytes()
            for link in (output.symlink_to, output.hardlink_to):
                link(source)
                with patch("sys.argv", ["channel_ir", str(manifest), "--output", str(output)]):
                    with self.assertRaises(OutputConflict):
                        main()
                self.assertEqual(source.read_bytes(), original)
                output.unlink()


if __name__ == "__main__":
    unittest.main()
