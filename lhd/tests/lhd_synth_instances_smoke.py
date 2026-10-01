#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Shared definitions, occurrence specialization, and incremental source edits.

USYN with a Liberty library runs native logical selection and then the separate
ABC technology map (pass.usyn.tmap=abc, the default). The fused envelope keeps
the schema-5 native decision report under qor.usyn (regions, cache counters,
per-region cache_key/artifact) and the technology-map report under qor.abc.
The logical cache lives in <workdir>/usyn_cache, the mapping cache beside it.
"""
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


def keys(report):
    return {row["module"]: row["cache_key"] for row in report["regions"]}


def costs(report):
    return {row["module"]: row["after"]["total"] for row in report["regions"]}


def decisions(report):
    # A content-addressed frozen artifact plus the region name identifies one decision.
    return {(row["module"], row["artifact"]["path"]) for row in report["regions"]}


def summary(report):
    return [(row["module"], row["cache_reused"], row["cache_key"][:16], row["artifact"]["path"][-24:])
            for row in report["regions"]]


def mapped(value):
    rows = value["qor"]["abc"]["regions"]
    return {"gates": sum(row["gates"] for row in rows), "area": round(sum(row["area"] for row in rows), 6)}


def synth(label, directory):
    value = run(label, base + ["--workdir", str(directory), "--emit", "verilog:" + str(root / (label + ".v"))])
    report, mapping = value["qor"]["usyn"], value["qor"]["abc"]
    # Synthesis proves nothing itself: prove() below is the separate `lhd lec`.
    # With Liberty, native selection is followed by the separate technology map.
    assert (report["schema_version"], report["kind"], report["target"], report["tmap"], report["output"]) == (
        5, "usyn", "cmos", "abc", "mapped-cmos"), (label, report)
    assert mapping["kind"] == "technology-map" and mapping["provider"] == "abc" and mapping["regions"], (label, mapping)
    assert mapping["incremental"]["enabled"] and mapping["incremental"]["regions"] == len(mapping["regions"]), (label, mapping)
    rows = report["regions"]
    assert rows and report["totals"]["regions"] == len(rows) and len(keys(report)) == len(rows), (label, report)
    assert all(row.get("artifact", {}).get("path") for row in rows), (label, rows)
    cache = report["cache"]
    hits = sum(row["cache_reused"] for row in rows)
    assert cache["enabled"] and (cache["invalid"], cache["refused"], cache["store_failures"]) == (0, 0, 0), (label, cache)
    assert (cache["reused"], cache["misses"], cache["stored"]) == (hits, len(rows) - hits, len(rows) - hits), (label, cache)
    for row in rows:
        entry = directory / "usyn_cache" / (row["cache_key"] + ".usyn-cache")
        assert entry.is_file(), (label, row["module"], entry)
    return value, report


def cold_run(label, directory):
    value, report = synth(label, directory)
    assert not any(row["cache_reused"] for row in report["regions"]), (label, report["cache"])
    assert report["cache"]["replayed_search_work"] == 0, (label, report["cache"])
    assert value["qor"]["abc"]["incremental"]["hits"] == 0, (label, value["qor"]["abc"]["incremental"])
    return value, report


def prove(label, directory):
    value = run(label, ["lec", "--impl", "lg:" + str(directory / "synth/net"), "--ref", "verilog:" + str(reference),
                        "--lib", "lg:" + str(root / "models"), "--top", "top", "--workdir", str(root / label)])
    assert value["lec"]["verdict"] == "proven" and value["lec"]["solver"] == "cvc5", (label, value)


work = root / "incremental"
write_design()
cold, cold_report = cold_run("cold", work)
assert (len(cold_report["regions"]) > 1) == (per_definition or per_occurrence), cold_report["regions"]
prove("cold-oracle", work)
for label in ("warm1", "warm2", "comment"):
    if label == "comment":
        source.write_text(source.read_text() + "// comment without semantic change\n")
    value, report = synth(label, work)
    # Every region replays its stored decision under the cold identity.
    assert all(row["cache_reused"] for row in report["regions"]), (label, report["cache"])
    assert keys(report) == keys(cold_report) and decisions(report) == decisions(cold_report), (label, report["regions"])
    assert costs(report) == costs(cold_report), (label, costs(report), costs(cold_report))
    assert report["cache"]["replayed_search_work"] > 0, (label, report["cache"])
    tmap = value["qor"]["abc"]["incremental"]
    assert tmap["hits"] == tmap["regions"] and tmap["misses"] == 0, (label, tmap)
    assert (root / (label + ".v")).read_bytes() == (root / "cold.v").read_bytes(), label
write_design(edited=True)
edited, edited_report = synth("edited", work)
# The edited definition is searched again under a new identity.
assert edited_report["cache"]["misses"], edited_report["cache"]
assert set(keys(edited_report).values()) - set(keys(cold_report).values()), (keys(edited_report), keys(cold_report))
prove("edited-oracle", work)
fresh = root / "fresh"
fresh_value, fresh_report = cold_run("fresh", fresh)
prove("fresh-oracle", fresh)
# Reusing unchanged regions must yield the same design as a fresh run.
assert costs(edited_report) == costs(fresh_report), (costs(edited_report), costs(fresh_report))
assert mapped(edited) == mapped(fresh_value), (edited["qor"]["abc"], fresh_value["qor"]["abc"])
# A semantic edit invalidates the only virtual-flat color in the default case.
# Definition boundaries and occurrence colors leave unchanged regions reusable.
evidence = ("edited", summary(edited_report), "cold", summary(cold_report), edited_report["cache"])
reused = [row for row in edited_report["regions"] if row["cache_reused"]]
if per_occurrence:
    # Only the `left` occurrence was edited: a color whose name and frozen
    # decision match the cold run must replay the cold cache entry.
    untouched = [row for row in edited_report["regions"] if (row["module"], row["artifact"]["path"]) in decisions(cold_report)]
    assert untouched, evidence
    assert all(row["cache_reused"] and row["cache_key"] == keys(cold_report)[row["module"]] for row in untouched), evidence
assert bool(reused) == (per_definition or per_occurrence), evidence
if per_definition:
    # The unedited FLIP=0 specialization is renamed once the FLIP=1 one
    # appears; its decision must still come from the cold entry.
    assert any(row["module"] not in keys(cold_report) for row in reused), evidence
print("shared-instance synthesis and independent flat-oracle LEC passed:", root)
