#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Source membership, per-operation pins, explicit exploration, proof and replay."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

work = Path(tempfile.mkdtemp(prefix="synth-attrs-", dir=os.environ.get("TEST_TMPDIR")))
lhd = str(Path("lhd/lhd").resolve())
lib = str(Path("inou/prp/tests/abc/test.lib").resolve())

def run(*args, ok=True):
    report = work / "result.json"
    report.unlink(missing_ok=True)
    result = subprocess.run([lhd, *map(str, args), "--result-json", str(report)],
                            text=True, capture_output=True, timeout=45)
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    return json.loads(report.read_text()), result.stdout + result.stderr

def synth(name, source, *settings):
    return run("synth", source, "--top", name, "--workdir", work / name,
               "--emit-dir", "lg:" + str(work / (name + "-net")),
               "--set", "synth.liberty=" + lib, "--set", "synth.opentimer=false",
               *[a for value in settings for a in ("--set", value)])

guided = work / "guided.prp"
guided.write_text('''pub comb guided::[timecheck=false](a:U16,b:U16,c:U16)->(x:U16,y:U16,z:U16) {
 {::[synth=(color="lane",grow=false),synth.adder="brent"] x=(a+b)#[0..=15] }
 {::[synth.color="lane",synth.adder="rca"] y=(b+c)#[0..=15] }
 mut right::[synth.color="other",synth.adder="cla"]=(a+c)#[0..=15]
 z=right
}''')
doc, log = synth("guided", guided)
rows = doc["qor"]["abc"]["regions"]
assert {r["module"].split("__g_")[-1] for r in rows} == {"lane", "other"}, rows
assert all(r["ware_trials"] == 0 for r in rows), rows
assert "synth-zero-overlap" in log
members = json.loads((work / "guided/synth/synth_groups.json").read_text())["definitions"]
pins = {n["source_policy"]["adder"]["value"] for d in members for n in d["members"]
        if "adder" in n.get("source_policy", {})}
assert pins == {"rca", "brent", "cla"}, pins
cold = {r["module"]: r["gates"] for r in rows}
warm, _ = synth("guided", guided)
assert {r["module"]: r["gates"] for r in warm["qor"]["abc"]["regions"]} == cold
assert all(r["cache"] == "hit" for r in warm["qor"]["abc"]["regions"])
run("pass", "liberty", "gensim", lib, "--emit-dir", "lg:" + str(work / "models"))
proof, _ = run("lec", "--impl", "lg:" + str(work / "guided-net"), "--ref", guided,
               "--lib", "lg:" + str(work / "models"), "--top", "guided.guided",
               "--workdir", work / "proof", "--set", "formal.timeout=10")
assert proof["lec"]["verdict"] == "proven", proof

for attrs, code in [("synth.foo=1", "synth-value"),
                    ('synth=(color="a"),synth.color="b"', "synth-duplicate")]:
    bad = work / "bad.prp"
    bad.write_text('pub comb bad::[timecheck=false](a:U8)->(y:U8) { {::[' + attrs + '] y=a+1 } }')
    _, log = run("compile", bad, "--workdir", work / "bad", ok=False)
    assert code in log, log

source = work / "auto.prp"
source.write_text('pub comb auto::[timecheck=false](a:U32,b:U32)->(y:Bool) { y=a<b }')
plain, _ = synth("auto", source)
assert sum(r["ware_trials"] for r in plain["qor"]["abc"]["regions"]) == 0
profile, _ = synth("auto", source, "synth.tune.profile=on", "synth.tune.validate=region")
assert sum(r["ware_trials"] for r in profile["qor"]["abc"]["regions"]) > 0
state = json.loads((work / "auto/synth_tune/state.json").read_text())
assert state["decisions"] and all(d["validation"] == "region" for d in state["decisions"].values())
replay, _ = synth("auto", source, "synth.tune.validate=region")
assert sum(r["ware_trials"] for r in replay["qor"]["abc"]["regions"]) == 0
assert sum(r["gates"] for r in replay["qor"]["abc"]["regions"]) == sum(r["gates"] for r in profile["qor"]["abc"]["regions"])
print("synthesis attributes: membership, mixed pins, mapped LEC, cache, profile and replay passed")

local = work / "local.prp"
local.write_text('''pub comb local::[timecheck=false](a:U16,b:U16,c:U16)->(x:U16,y:U16) {
 mut requested::[synth.adder="auto"]=(a+b)#[0..=15]
 x=requested
 y=(b+c)#[0..=15]
}''')
local_profile, _ = synth("local", local, "synth.adder=brent", "synth.tune.profile=on", "synth.tune.attempts=1")
assert sum(r["ware_trials"] for r in local_profile["qor"]["abc"]["regions"]) > 0
applied = json.loads((work / "local/synth_tune/applied.json").read_text())
assert any(o["choices"]["adder"]["value"] == "brent"
           for r in applied["regions"].values() for o in r.get("operations", [])), applied

shared = work / "shared.prp"
shared.write_text('''comb lane::[synth.color="carry"](a:U8,b:U8)->(y:U8) { y=(a+b)#[0..=7] }
pub comb shared::[timecheck=false](a:U8,b:U8,c:U8)->(x:U8,y:U8) {
 x=lane(a=a,b=b).y
 y=lane(a=b,b=c).y
}''')
shared_doc, _ = synth("shared", shared, "compile.upass.inline=false")
assert len([r for r in shared_doc["qor"]["abc"]["regions"] if "__g_carry" in r["module"]]) == 1, shared_doc
shared_groups = json.loads((work / "shared/synth/synth_groups.json").read_text())
assert any(d["physical_instances"] == 2 for d in shared_groups["definitions"]), shared_groups

for validation in ("structural", "region", "design"):
 native, _ = synth("auto", source, "synth.mapper=usyn", "pass.usyn.tmap=none",
                   "synth.tune.profile=on", "synth.tune.validate=" + validation,
                   "synth.tune.attempts=1")
 store = json.loads((work / "auto/synth_tune/state.json").read_text())
 assert any(k.startswith("usyn-") and v["validation"] == validation
            for k,v in store["decisions"].items()), store
print("local auto, shared implementation reuse and native mapper validation passed")

cycle = work / "cycle.prp"
cycle.write_text('''pub comb cycle::[timecheck=false](a:U8,b:U8,c:U8,d:U8)->(y:U8) {
 mut first::[synth.color="a"]=(a+b)#[0..=7]
 mut middle::[synth.color="b"]=first^c
 mut last::[synth.color="a"]=(middle+d)#[0..=7]
 y=last
}''')
cyclic, _ = synth("cycle", cycle)
assert {r["module"].split("__g_")[-1] for r in cyclic["qor"]["abc"]["regions"]} == {"a", "b"}
proof, _ = run("lec", "--impl", "lg:" + str(work / "cycle-net"), "--ref", cycle,
               "--lib", "lg:" + str(work / "models"), "--top", "cycle.cycle",
               "--workdir", work / "cycle-proof", "--set", "formal.timeout=10")
assert proof["lec"]["verdict"] == "proven", proof

inline = work / "inline.prp"
inline.write_text('''comb lane::[synth.color="inside"](a:U8,b:U8)->(y:U8) { y=(a+b)#[0..=7] }
pub comb inline::[timecheck=false](a:U8,b:U8,c:U8)->(x:U8,y:U8) {
 x=lane(a=a,b=b).y
 y=(a-c)#[0..=7]
}''')
synth("inline", inline)
inline_groups = json.loads((work / "inline/synth/synth_groups.json").read_text())
assert any("color" not in n.get("source_policy", {}) for d in inline_groups["definitions"] for n in d["members"])
print("region contraction A -> B -> A and module-scope isolation passed")
