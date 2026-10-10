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

# A module-level color covers compact-loop bodies. Call-site specialization must
# keep the loop descriptor: otherwise the loop index port is left undriven and
# the flattened comparisons have no operands ("LT/GT requires an operand in each bank").
looped = work / "looped.prp"
looped.write_text('''pub comb looped::[timecheck=false,synth.color="all"](request:U16)->(grant:U16) {
 mut can_grant:U16 = 0
 for i in 0..<16 {
  mut wins = true
  for j in 0..<16 {
   if i < j {
    wins = wins and (request#[j] == 0)
   } elif i > j {
    wins = wins and (request#[j] == 0)
   }
  }
  can_grant#[i] = U1(wins)
 }
 grant = request & can_grant
}''')
synth("looped", looped)
proof, _ = run("lec", "--impl", "lg:" + str(work / "looped-net"), "--ref", looped,
               "--lib", "lg:" + str(work / "models"), "--top", "looped.looped",
               "--workdir", work / "looped-proof", "--set", "formal.timeout=10")
assert proof["lec"]["verdict"] == "proven", proof

# A runtime shift keeps its amount-shaping wiring (`select << 5`, a constant add)
# in the shifter's region, so the known-zero low bits prune the barrel. Without it
# an unrelated policy attribute inflated this 512-bit mux ~6x.
def total_gates(doc):
    return sum(r["gates"] for r in doc["qor"]["abc"]["regions"])

mux_plain = work / "mux_plain.prp"
mux_plain.write_text('''pub comb mux_plain::[timecheck=false](a:U512,s:U4)->(o:U32) { o=(a >> (s*32))#[0..<32] }''')
mux_pin = work / "mux_pin.prp"
mux_pin.write_text('''pub comb mux_pin::[timecheck=false,synth.multiplier="tdm"](a:U512,s:U4)->(o:U32) { o=(a >> (s*32))#[0..<32] }''')
plain_doc, _ = synth("mux_plain", mux_plain)
pin_doc, _ = synth("mux_pin", mux_pin)
assert total_gates(pin_doc) == total_gates(plain_doc), (total_gates(pin_doc), total_gates(plain_doc))

# A constant factor is strength-reduced ahead of the multiplier architecture.
cmul = work / "cmul.prp"
cmul.write_text('''pub comb cmul::[timecheck=false](a:U8)->(x:U16,y:U16,z:U16,w:U16) { x=a*10
 y=a*129
 z=a*127
 w=a*85 }''')
gates = {}
for m in ("array", "tdm", "csa", "tree"):
    src = work / f"cmul_{m}.prp"
    src.write_text(f'''pub comb cmul_{m}::[timecheck=false](a:U8)->(x:U16,y:U16,z:U16,w:U16) {{ x=a*10
 y=a*129
 z=a*127
 w=a*85 }}''')
    gates[m] = total_gates(synth(f"cmul_{m}", src, "synth.multiplier=" + m)[0])
assert len(set(gates.values())) == 1, gates
synth("cmul", cmul)
proof, _ = run("lec", "--impl", "lg:" + str(work / "cmul-net"), "--ref", cmul,
               "--lib", "lg:" + str(work / "models"), "--top", "cmul.cmul",
               "--workdir", work / "cmul-proof", "--set", "formal.timeout=10")
assert proof["lec"]["verdict"] == "proven", proof

# Exploration ranks regions and stops early: a big adder and a small one. With the
# area objective every alternative loses to rca, so patience=1 must stop after the
# first reject, and a high share floor must skip the small region entirely.
def explore(tag, *settings):
    src = work / f"rank_{tag}.prp"
    src.write_text(f'''pub comb rank_{tag}::[timecheck=false](a:U64,b:U64,c:U9,d:U9)->(x:U64,y:U9) {{
 x=(a+b)#[0..=63]
 y=(c+d)#[0..=8]
}}''')
    doc, _ = synth(f"rank_{tag}", src, "synth.tune.profile=on", *settings)
    return sum(r["ware_trials"] for r in doc["qor"]["abc"]["regions"])

full = explore("full", "synth.tune.patience=0", "synth.tune.min_share=0")
assert full > 2, full
assert explore("patient", "synth.tune.patience=1", "synth.tune.min_share=0") < full
assert explore("share", "synth.tune.patience=0", "synth.tune.min_share=0.5") < full
bad_doc, bad_log = run("synth", work / "rank_full.prp", "--top", "rank_full", "--set", "synth.liberty=" + lib,
                      "--set", "synth.tune.min_share=2", ok=False)
assert "min_share" in (json.dumps(bad_doc) + bad_log), (bad_doc, bad_log)

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
