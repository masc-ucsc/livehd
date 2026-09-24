#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Shared definitions, occurrence specialization, and incremental source edits."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(tempfile.mkdtemp(prefix="synth-instances-", dir=os.environ.get("TEST_TMPDIR")))
lhd = str(Path("lhd/lhd").resolve())
monitor = str(Path("pass/usyn/measure_synth").resolve())
source = root / "instances.v"
reference = root / "reference.v"
library = "inou/prp/tests/abc/test.lib"
mode = sys.argv[1] if len(sys.argv) > 1 else "default"
if mode not in ("default", "definitions", "occurrences") or len(sys.argv) > 2:
    raise SystemExit("usage: lhd_synth_instances_smoke.py [default|definitions|occurrences]")
per_definition = mode == "definitions"
per_occurrence = mode == "occurrences"


def write_design(edited=False):
    source.write_text('''module lane #(parameter FLIP=0)(input a,b,c, output p,n,y);
wire t = a & b;
assign p = FLIP ? (a | b) : t;
assign n = ~p;
assign y = p & c;
endmodule
module top(input a,b,c,d, output lp,ln,ly,rp,rn,ry,sp,sn,sy);
lane #(.FLIP(EDIT)) left(a,b,c,lp,ln,ly);
lane #(.FLIP(0)) right(b,c,d,rp,rn,ry);
lane #(.FLIP(0)) spare(c,d,a,sp,sn,sy);
endmodule
'''.replace("EDIT", "1" if edited else "0"))
    # Flat hand-written oracle does not reuse parameter elaboration or hierarchy.
    reference.write_text('''module top(input a,b,c,d, output lp,ln,ly,rp,rn,ry,sp,sn,sy);
assign lp = a OP b;
assign ln = ~lp;
assign ly = lp & c;
assign rp = b & c;
assign rn = ~rp;
assign ry = rp & d;
assign sp = c & d;
assign sn = ~sp;
assign sy = sp & a;
endmodule
'''.replace("OP", "|" if edited else "&"))


def run(label, args):
    envelope = root / (label + ".result.json")
    archive = root / (label + ".measurement")
    command = [lhd, *args, "--result-json", str(envelope), "-q"]
    result = subprocess.run([monitor, "--archive", str(archive), "--seconds", "20", "--memory-mb", "4096",
                             "--", *command], capture_output=True, text=True, timeout=25)
    log = (archive / "command.log").read_text()
    assert result.returncode == 0, (label, result.stdout, result.stderr, log, envelope.read_text() if envelope.exists() else "no result")
    value = json.loads(envelope.read_text())
    assert value["status"] == "pass", (label, value)
    return value


run("models", ["pass", "liberty", "gensim", library, "--emit-dir", "lg:" + str(root / "models")])
base = ["synth", str(source), "--top", "top", "--set", "compile.upass.inline=false",
        "--set", "synth.mapper=usyn", "--set", "synth.liberty=" + library, "--set", "synth.opentimer=false"]
if per_definition:
    base += ["--set", "pass.color.hier=false"]
if per_occurrence:
    # The synth profile colors flop to flop, which ignores max_gate: opt out.
    base += ["--set", "pass.color.synth.min_ge=0", "--set", "pass.color.synth.max_gate=2",
             "--set", "pass.color.synth.flop_to_flop=false"]


def synth(label, directory):
    value = run(label, base + ["--workdir", str(directory), "--emit", "verilog:" + str(root / (label + ".v"))])
    report = value["qor"]["usyn"]
    # Synthesis proves nothing itself: prove() below is the separate `lhd lec`.
    rows = report["regions_searched"] + [row["decision"] for row in report["regions_reused"]]
    assert rows and all(row["status"] == "abc_opt" for row in rows), (label, report)
    return value, report


def prove(label, directory):
    value = run(label, ["lec", "--impl", "lg:" + str(directory / "synth/net"), "--ref", "verilog:" + str(reference),
                        "--lib", "lg:" + str(root / "models"), "--top", "top", "--workdir", str(root / label)])
    assert value["lec"]["verdict"] == "proven" and value["lec"]["solver"] == "cvc5", (label, value)


work = root / "incremental"
write_design()
cold, cold_report = synth("cold", work)
assert cold_report["regions_searched"], cold_report
assert (len(cold_report["regions_searched"]) + len(cold_report["regions_reused"]) > 1) == (per_definition or per_occurrence), cold_report
prove("cold-oracle", work)
for label in ("warm1", "warm2", "comment"):
    if label == "comment":
        source.write_text(source.read_text() + "// comment without semantic change\n")
    _, report = synth(label, work)
    assert report["regions_reused"] and not report["regions_searched"], (label, report)
    assert (root / (label + ".v")).read_bytes() == (root / "cold.v").read_bytes(), label
write_design(edited=True)
edited, edited_report = synth("edited", work)
assert edited_report["regions_searched"], edited_report
# A semantic edit invalidates the only virtual-flat color in the default case.
# Definition boundaries and occurrence colors leave unchanged regions reusable.
assert bool(edited_report["regions_reused"]) == (per_definition or per_occurrence), edited_report
if per_definition:
    assert any(row["region"] != row["cached_region"] for row in edited_report["regions_reused"]), edited_report
prove("edited-oracle", work)
fresh = root / "fresh"
fresh_value, fresh_report = synth("fresh", fresh)
prove("fresh-oracle", fresh)
# Reusing unchanged regions must yield the same mapped design as a fresh run.
assert ({k: edited["qor"]["abc"]["total"][k] for k in ("gates", "area")}
        == {k: fresh_value["qor"]["abc"]["total"][k] for k in ("gates", "area")}), (edited["qor"], fresh_value["qor"])
print("shared-instance synthesis and independent flat-oracle LEC passed:", root)
