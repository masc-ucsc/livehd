# SPDX-License-Identifier: BSD-3-Clause
"""`lhd synth --gui` URoute congestion map (2i-interact): offered only with the learned model loaded."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import urllib.request

from tools.uroute.export_weights import encode_tensors
from tools.uroute.features import Maps
from tools.uroute.fixture import tensors
from tools.uroute.maps import read
from tools.uroute.pool import publish_sample, publish_weights

root = Path(os.environ["TEST_TMPDIR"]) / "uroute_gui"
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
# A first batch run exports the features the support sample needs, then the pool is published (as lhd_smoke does).
subprocess.run(base + ["--workdir", str(root / "seed"), "--set", "synth.place.route.model=uroute"], check=True, capture_output=True, timeout=30)
export = root / "seed/synth/place/uroute_export"
metadata = json.loads((export / "metadata.json").read_text())
features = read(export / "features.bin")
area = 256 * 256
labels = Maps(3, 256, 256, False, [2.] * area + [0.] * area + [1.] * area, [(2., 2.), (0., 0.), (1., 1.)])
pool = root / "pool"
publish_sample(pool, {"top": metadata["top"], "technology": metadata["technology"], "floorplan": metadata["floorplan"],
                      "created_at": 1, "status": "routed", "source": "livehd"}, features, labels)
publish_weights(pool, encode_tensors(tensors()), trained_on="CLI-test-only", provenance={"source": "synthetic-test-only"})


def session(extra, kind):
    log = root / ("gui_" + kind + ".log")
    # synth.place.gui.pause: the GUI runs untouched by default; this test needs it to stop at a gate.
    proc = subprocess.Popen(base + ["--workdir", str(root / ("W_" + kind)), "--gui", "--gui-no-open",
                                    "--set", "synth.place.gui.pause=true"] + extra, stdout=subprocess.DEVNULL, stderr=open(log, "w"))
    url = None
    for _ in range(600):
        time.sleep(0.05)
        m = re.search(r"http://127\S+", log.read_text())
        if m:
            url = m.group(0)
            break
    assert url, log.read_text()
    host, token = url.split("/?t=")
    get = lambda path, raw=False: (lambda d: d if raw else json.loads(d))(
        urllib.request.urlopen(f"{host}{path}{'&' if '?' in path else '?'}t={token}", timeout=20).read())
    post = lambda c: json.loads(urllib.request.urlopen(urllib.request.Request(
        f"{host}/cmd?t={token}", data=json.dumps(c).encode(), method="POST"), timeout=20).read())
    try:
        end = time.time() + 60
        while get("/state")["state"] != "gate":
            assert time.time() < end
            time.sleep(0.02)
        assert post({"cmd": "map", "kind": "uroute"})["ok"]
        while True:
            s = get("/state")
            if s["map"] == "uroute" and s["map_v"] > 0:
                break
            assert time.time() < end, s
            time.sleep(0.02)
        raw = get("/map", raw=True)
        bx, by, peak = struct.unpack("<IIf", raw[4:16])
        out = (bx, by, peak, s["map_note"])
        assert post({"cmd": "finish"})["ok"]
        assert proc.wait(timeout=60) == 0
        return out
    finally:
        if proc.poll() is None:
            proc.kill()


bx, by, peak, note = session([], "off")  # no model: an empty map and the reason
assert bx == 0 and "not loaded" in note, (bx, note)
bx, by, peak, note = session(["--set", "synth.place.route.model=uroute", "--set", f"synth.place.uroute.dir={pool}", "--set", "synth.place.uroute.shots=1"], "on")
assert bx == by and bx >= 8 and note == "", (bx, by, peak, note)
