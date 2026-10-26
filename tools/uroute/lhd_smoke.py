# SPDX-License-Identifier: BSD-3-Clause
"""Real CLI integration using repository-owned technology and synthetic support."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct

from tools.uroute.export_weights import encode_tensors
from tools.uroute.features import Maps
from tools.uroute.fixture import tensors
from tools.uroute.maps import read
from tools.uroute.pool import publish_sample, publish_weights

root = Path(os.environ["TEST_TMPDIR"]) / "uroute_cli"
root.mkdir()
source = root / "dut.v"
source.write_text("module dut(input [3:0] a,b,output [3:0] y);assign y=a^b;endmodule\n")
tech = Path("inou/lefdef/tests/data")
base = ["lhd/lhd", "synth", str(source), "--top", "dut", "-q",
        "--set", f"synth.liberty={tech}/sky130hd.lib",
        "--set", f"synth.lef={tech}/sky130hd.tlef,{tech}/sky130hd.lef",
        "--set", "synth.place=true", "--set", "synth.opentimer=false",
        "--set", "synth.place.timing_driven=false", "--set", "synth.place.resize=false",
        "--set", "synth.place.io_resynth=0", "--set", "synth.place.grid.density=8",
        "--set", "synth.place.iterations=100", "--set", "synth.place.density=0.9",
        "--set", "synth.place.legalize.bin.max_density=1"]


def run(name, settings, error=None):
    work = root / name
    report = root / (name + ".json")
    command = base + ["--workdir", str(work), "--result-json", str(report)]
    for key, value in settings.items():
        command += ["--set", f"synth.place.{key}={value}"]
    result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    assert report.is_file(), (result.returncode, result.stdout, result.stderr)
    data = json.loads(report.read_text())
    if error:
        assert result.returncode != 0 and data["error"]["class"] == "usage", data
        assert error in data["error"]["message"], data
        return
    assert result.returncode == 0, (data, result.stderr)
    directory = work / "synth/place"
    return (json.loads((directory / "gp_history.json").read_text()),
            json.loads((directory / "route_options.json").read_text()), directory)


for name, settings, error in [
    ("bad_shots", {"uroute.shots": "17"}, "URoute"),
    ("bad_min", {"uroute.shots": "2", "uroute.min_shots": "3"}, "URoute"),
    ("bad_fallback", {"uroute.fallback": "random"}, "URoute"),
    ("bad_level", {"uroute.drc_level": "-1"}, "URoute"),
    ("bad_hold", {"final.route.uroute_hold": "0"}, "final.route"),
    ("bad_switch", {"route.model": "auto", "final.route.switch": ".001"}, "final.route"),
    ("bad_probe", {"fp.route.probe": "sometimes"}, "fp.route.probe"),
]:
    run(name, settings, error)

history, options, directory = run("empty", {"route.model": "uroute"})
assert history["route_model"] == "rsmt" and "support" in history["uroute_skipped"], history
empty_qor = json.loads((directory / "qor.json").read_text())
assert empty_qor["route_model"] == "rsmt" and "support" in empty_qor["uroute_skipped"], empty_qor
export = directory / "uroute_export"
metadata = json.loads((export / "metadata.json").read_text())
features = read(export / "features.bin")
assert (features.channels, features.bins_x, features.bins_y) == (4, 256, 256)
assert metadata["route_grid"] == [8, 8] and metadata["capacity_h"] > 0 and metadata["capacity_v"] > 0
assert metadata["features_sha256"] == hashlib.sha256((export / "features.bin").read_bytes()).hexdigest()
assert Path(options["uroute"]["dir"]).name == "pretrained", options
pool = root / "pool"
area = 256 * 256
labels = Maps(3, 256, 256, False, [2.] * area + [0.] * area + [1.] * area, [(2., 2.), (0., 0.), (1., 1.)])
sample = publish_sample(pool, {"top": metadata["top"], "technology": metadata["technology"],
                              "floorplan": metadata["floorplan"], "created_at": 1,
                              "status": "routed", "source": "livehd"}, features, labels)
publish_weights(pool, encode_tensors(tensors()), trained_on="CLI-test-only", provenance={"source": "synthetic-test-only"})
settings = {"route.model": "uroute", "uroute.dir": str(pool), "uroute.shots": "1"}
history, options, directory = run("active", settings)
assert history["route_model"] == "uroute" and not history["uroute_skipped"], history
assert history["uroute_sample_ids"] == [sample["id"]], history
assert all(s["route_model"] == "uroute" for s in history["samples"]), history
assert options["uroute"]["sample_ids"] == [sample["id"]]
assert history["uroute_weights_sha256"] == options["uroute"]["weights_sha256"]
assert history["uroute_manifest_sha256"] == options["uroute"]["manifest_sha256"]
assert len(history["uroute_weights_sha256"]) == len(history["uroute_manifest_sha256"]) == 64
assert options["stages"]["final"]["uroute_ladder"]
assert not options["stages"]["fp"]["uroute_ladder"] and not options["stages"]["macro"]["uroute_ladder"]
assert options["stages"]["fp"]["model"] == "rsmt", options
active_identity = options["uroute"]["cache_identity"]
qor = json.loads((directory / "qor.json").read_text())
assert qor["uroute_sample_ids"] == [sample["id"]] and not qor["uroute_skipped"], qor
assert qor["route_model"] == "uroute", qor
# Measure warm reuse twice after the unchanged build, as required by the cache
# contract. Selected support IDs remain part of the placement identity.
for _ in range(2):
    history, options, directory = run("active", settings)
    warm_qor = json.loads((directory / "qor.json").read_text())
assert warm_qor["incremental"]["hit"], warm_qor
history, options, directory = run("automatic", dict(settings, **{"route.model": "auto", "final.route.switch": "1000",
                                                                  "final.route.uroute_switch": "1000", "final.route.uroute_hold": "1"}))
assert history["route_model"] == "uroute", history
assert history["route_uroute_switch_iteration"] > history["route_switch_iteration"] > 0, history
assert history["route_switch_uroute_rsmt"] >= 0 and history["route_switch_uroute"] >= 1, history
history, options, directory = run("probe", dict(settings, **{"route.model": "rsmt", "final.route.probe": "uroute",
                                                           "final.route.probe_iterations": "20", "stop_overflow": ".2"}))
assert any(s["route_probe"] for s in history["samples"]), history
assert history["route_model"] == "uroute" and (directory / "uroute_export/final_probe/metadata.json").exists(), history
assert options["stages"]["final"]["probe_model"] == "uroute", options
history, options, directory = run("automatic_convergence", dict(settings, **{"route.model": "auto", "stop_overflow": ".2"}))
assert history["route_model"] == "uroute" and history["route_uroute_switch_iteration"] > 0, history
assert not history["converged"], "RS convergence skipped the pending learned rung"
# A repaired or incompatible data directory must not reuse a placement produced
# from another pool revision under the same option strings.
manifest_path = pool / "manifest.json"
original_manifest = manifest_path.read_bytes()
manifest = json.loads(manifest_path.read_text())
manifest["arch_version"] = 999
manifest_path.write_text(json.dumps(manifest))
history, options, directory = run("active", settings)
assert history["route_model"] == "rsmt" and "version" in history["uroute_skipped"], history
assert options["uroute"]["cache_identity"] != active_identity
assert not json.loads((directory / "qor.json").read_text())["incremental"]["hit"]
manifest_path.write_bytes(original_manifest)
history, options, directory = run("disabled", {"route.model": "uroute", "uroute.dir": ""})
assert history["route_model"] == "rsmt" and "disabled" in history["uroute_skipped"], history
assert not (directory / "uroute_export").exists()
assert not options["uroute"]["export"]
history, options, directory = run("no_export", dict(settings, **{"uroute.export": "false"}))
assert history["route_model"] == "uroute" and not (directory / "uroute_export").exists(), history
# Whole-flow physical replay precedes the placer's own cache lookup. Its key
# must also include pool contents, not just the unchanged directory option.
sdc = root / "virtual.sdc"
sdc.write_text("create_clock -name virtual -period 10\n")
base[base.index("synth.opentimer=false")] = "synth.opentimer=true"
base[base.index("synth.place.io_resynth=0")] = "synth.place.io_resynth=5"
base += ["--set", f"synth.sdc={sdc}"]
for _ in range(3):
    run("physical_active", settings)
physical = json.loads((root / "physical_active.json").read_text())["qor"]["place"]
assert physical["reused_final_state"], physical
manifest_path.write_text(json.dumps(manifest))  # Same incompatible revision as above.
history, options, directory = run("physical_active", settings)
physical = json.loads((root / "physical_active.json").read_text())["qor"]["place"]
assert not physical.get("reused_final_state", False), physical
assert history["route_model"] == "rsmt" and "version" in history["uroute_skipped"], history
manifest_path.write_bytes(original_manifest)
for _ in range(2):
    history, options, directory = run("physical_active", settings)
physical = json.loads((root / "physical_active.json").read_text())["qor"]["place"]
assert physical["reused_final_state"], physical
previous_weights = options["uroute"]["weights_sha256"]
updated = tensors()
updated["output.bias"] = ((1,), struct.pack("<f", .3))
publish_weights(pool, encode_tensors(updated), trained_on="CLI-test-only",
                provenance={"source": "synthetic-test-only"})
history, options, directory = run("physical_active", settings)
physical = json.loads((root / "physical_active.json").read_text())["qor"]["place"]
assert not physical.get("reused_final_state", False), physical
assert options["uroute"]["weights_sha256"] != previous_weights
assert history["route_model"] == "uroute" and not history["uroute_skipped"], history
# A weightless pool can change its validation failure without changing the
# manifest. Whole-flow replay must refresh the reported fallback reason too.
weightless = json.loads(manifest_path.read_text())
weightless["weights"] = None
manifest_path.write_text(json.dumps(weightless))
for _ in range(3):
    history, options, directory = run("physical_weightless", settings)
physical = json.loads((root / "physical_weightless.json").read_text())["qor"]["place"]
assert physical["reused_final_state"] and "no trained weights" in history["uroute_skipped"], history
feature_path = pool / sample["directory"] / "features.bin"
feature_bytes = feature_path.read_bytes()
feature_path.write_bytes(feature_bytes[:-1])
history, options, directory = run("physical_weightless", settings)
physical = json.loads((root / "physical_weightless.json").read_text())["qor"]["place"]
assert not physical.get("reused_final_state", False), physical
assert history["route_model"] == "rsmt" and "digest mismatch" in history["uroute_skipped"], history
feature_path.write_bytes(feature_bytes)
history, options, directory = run("physical_weightless", settings)
physical = json.loads((root / "physical_weightless.json").read_text())["qor"]["place"]
assert not physical.get("reused_final_state", False), physical
assert "no trained weights" in history["uroute_skipped"], history
history, options, directory = run("physical_weightless", settings)
physical = json.loads((root / "physical_weightless.json").read_text())["qor"]["place"]
assert physical["reused_final_state"] and "no trained weights" in history["uroute_skipped"], history
# Routed ingestion includes the generated memory-proxy LEF. The unchanged
# source must match that same technology without adding its output as an input.
memory_source = root / "memory.v"
memory_source.write_text("module ram(input clk,en,input [2:0] wa,ra,rb,input [3:0] din,output [3:0] y);"
                         "reg [3:0] mem [0:7];always @(posedge clk) if(en) mem[wa]<=din;"
                         "assign y=mem[ra]^mem[rb];endmodule\n")
base[base.index(str(source))] = str(memory_source)
base[base.index("dut")] = "ram"
base += ["--set", "synth.memory=macro"]
history, options, directory = run("memory_seed", {"route.model": "rsmt"})
memory_export = directory / "uroute_export"
memory_metadata = json.loads((memory_export / "metadata.json").read_text())
proxy_bytes = (directory / "memory_proxies.lef").read_bytes()
lef_hashes = sorted(hashlib.sha256(data).hexdigest() for data in
                    [(tech / "sky130hd.tlef").read_bytes(), (tech / "sky130hd.lef").read_bytes(), proxy_bytes])
memory_technology = "lef:" + hashlib.sha256(":".join(lef_hashes).encode()).hexdigest()
assert memory_metadata["technology"] == memory_technology, memory_metadata
memory_pool = root / "memory_pool"
memory_labels = Maps(3, 256, 256, False, [.5] * area + [0.] * area + [1.] * area,
                     [(.5, .5), (0., 0.), (1., 1.)])
memory_sample = publish_sample(memory_pool, {"top": "ram", "technology": memory_technology,
                               "floorplan": memory_metadata["floorplan"], "created_at": 1,
                               "status": "routed", "source": "livehd"},
                               read(memory_export / "features.bin"), memory_labels)
publish_weights(memory_pool, encode_tensors(tensors()), trained_on="CLI-test-only",
                provenance={"source": "synthetic-test-only"})
memory_settings = {"route.model": "uroute", "uroute.dir": str(memory_pool), "uroute.shots": "1",
                   "uroute.fallback": "skip"}
history, options, directory = run("memory_active", memory_settings)
assert history["route_model"] == "uroute" and not history["uroute_skipped"], history
assert history["uroute_sample_ids"] == [memory_sample["id"]], history
assert (directory / "memory_proxies.lef").read_bytes() == proxy_bytes
assert json.loads((directory / "uroute_export/metadata.json").read_text())["technology"] == memory_technology
print("URoute CLI options, support loading, fallback, history and final feature export passed")
