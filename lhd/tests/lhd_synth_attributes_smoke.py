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
    timer = "synth.opentimer=true" if "synth.opentimer=true" in settings else "synth.opentimer=false"
    settings = [v for v in settings if v != "synth.opentimer=true"]
    return run("synth", source, "--top", name, "--workdir", work / name,
               "--emit-dir", "lg:" + str(work / (name + "-net")),
               "--set", "synth.liberty=" + lib, "--set", timer,
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
profile, profile_log = synth("auto", source, "synth.tune.profile=on", "synth.tune.validate=region")
pins = [json.loads(l) for l in profile_log.splitlines() if '"synth-tune-pin"' in l]
assert pins and all(Path(p["span"]["file"]).name == "auto.prp" and p["span"]["start_line"] == 1 for p in pins), pins
assert all("pin it with ::[synth." in p["message"] for p in pins), pins
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

# Cut conditions are source attributes. The default profile extracts a wide add
# (a ware module); `synth.stop_arith=false` on its variable keeps it inline while an
# untouched add is still extracted, and under the coarse profile `stop_arith=true`
# forces a cut the profile would not make.
def sum_regions(tag, attr, *settings):
    src = work / f"stop_{tag}.prp"
    mark = f"::[{attr}]" if attr else ""
    src.write_text(f'''pub comb stop_{tag}::[timecheck=false](a:U32,b:U32,c:U32)->(x:U32,y:U32) {{
 mut s{mark} = (a+b)#[0..=31]
 x = s ^ c
 y = (a-c)#[0..=31]
}}''')
    doc, _ = synth(f"stop_{tag}", src, *settings)
    regions = doc["qor"]["abc"]["regions"]
    return len([r for r in regions if "__ware_sum" in r["module"]]), len(regions)

default_sums, _ = sum_regions("d", "")
assert default_sums == 2, default_sums
kept_sums, _ = sum_regions("k", "synth.stop_arith=false")
assert kept_sums == default_sums - 1, (kept_sums, default_sums)
_, coarse_regions = sum_regions("c0", "", "synth.partition=coarse")
_, coarse_cut = sum_regions("c1", "synth.stop_arith=true", "synth.partition=coarse")
assert coarse_cut > coarse_regions, (coarse_cut, coarse_regions)
for bad_attr, code in [("synth.stop_arith=1", "synth-value"), ("synth.stop_bogus=true", "synth-value")]:
    bad = work / "bad_stop.prp"
    bad.write_text('pub comb bad_stop::[timecheck=false](a:U8)->(y:U8) { {::[' + bad_attr + '] y=a+1 } }')
    _, log = run("compile", bad, "--workdir", work / "bad_stop", ok=False)
    assert code in log, log

# A callee that carries a declaration attribute list is not inlined; a bare
# positional actual must then fill the next input NOT bound by name, not slot 0.
mixed = work / "mixed_args.prp"
mixed.write_text('''comb mixed_sw::[synth.adder="cla"](amount:U5,arithmetic:Bool,v:U32)->(r:U32) { wrap r = v << amount }
pub comb mixed_args::[timecheck=false](a:U32,b:U32,arithmetic:Bool)->(y:U32) {
 y=mixed_sw(amount=b#[0..=4],arithmetic,v=a)
}''')
synth("mixed_args", mixed)

# An attributed callee called from inside another inlined function: the call used
# to keep the inline frame's tag (`undefined function 'inl1_f'`).
nested = work / "nested_attr.prp"
nested.write_text('''comb nested_sw::[synth.adder="cla"](amount:U5,v:U32)->(r:U32) { wrap r = v << amount }
comb nested_mid(a:U32,b:U32)->(s:U32,t:U32) {
 s=nested_sw(amount=b#[0..=4],v=a)
 t=nested_sw(amount=b#[4..=8],v=a)
}
pub comb nested_attr::[timecheck=false](a:U32,b:U32)->(y:U32,z:U32) {
 const p=nested_mid(a=a,b=b)
 y=p.s
 z=p.t
}''')
synth("nested_attr", nested)

# Attribute diagnostics point at the attribute's own file and line.
unused = work / "unused_attr.prp"
unused.write_text('''pub comb unused_attr::[timecheck=false](a:U8,b:U8)->(y:U8,z:U8) {
 {
  ::[synth.multiplier="tdm"]
  y=(a+b)#[0..=7]
 }
 mut k::[synth.color="dead"]=3
 z=k
}''')
_, unused_log = synth("unused_attr", unused)
located = {}
for line in unused_log.splitlines():
    try:
        diag = json.loads(line)
    except ValueError:
        continue
    if str(diag.get("code", "")).startswith("synth-"):
        span = diag.get("span") or {}
        located[diag["code"]] = (Path(span.get("file", "")).name, span.get("start_line"))
assert located.get("synth-attr-unused") == ("unused_attr.prp", 3), located
assert located.get("synth-empty-anchor") == ("unused_attr.prp", 6), located
unused_groups = json.loads((work / "unused_attr/synth/synth_groups.json").read_text())["definitions"]
assert any(m.get("file", "").endswith("unused_attr.prp") and m.get("line") for d in unused_groups for m in d["members"]), unused_groups

inline = work / "inline.prp"
inline.write_text('''comb lane::[synth.color="inside"](a:U8,b:U8)->(y:U8) { y=(a+b)#[0..=7] }
pub comb inline::[timecheck=false](a:U8,b:U8,c:U8)->(x:U8,y:U8) {
 x=lane(a=a,b=b).y
 y=(a-c)#[0..=7]
}''')
synth("inline", inline)
inline_groups = json.loads((work / "inline/synth/synth_groups.json").read_text())
assert any("color" not in n.get("source_policy", {}) for d in inline_groups["definitions"] for n in d["members"])
# A per-declaration memory pin overrides the pass-wide fold mode: flops folds a
# memory that memory_max_bits would keep native, macro keeps one memory=true folds.
def memory_regions(pin, fold, *extra):
    src = work / ("mem_" + pin + ".prp")
    src.write_text('pub mod mem_' + pin + '::[timecheck=false](a:U3,d:U8,we:Bool,clk:Clock)->(o:U8@[0]) {\n'
                   '  reg q:[8]U8:[clock_pin=clk' + (',synth.memory="' + pin + '"' if pin != "none" else "") + ']\n'
                   '  if we { q[a] = d }\n  o = q[a]\n}')
    doc, _ = synth("mem_" + pin, src, "pass.abc.memory=" + fold, *extra)
    table = doc["qor"]["abc"]["memories"]
    assert len(table) == 1 and table[0]["name"] == "q" and table[0]["size"] == 8 and table[0]["bits"] == 8, table
    assert table[0]["realization"] == ("flops" if table[0]["reason"] != "" and any(
        m.endswith("_blasted") for m in {r["module"] for r in doc["qor"]["abc"]["regions"]}) else "native"), table
    memory_reasons.append(table[0]["reason"])
    return {r["module"] for r in doc["qor"]["abc"]["regions"]}
memory_reasons = []
assert not any(m.endswith("_blasted") for m in memory_regions("none", "auto", "pass.abc.memory_max_bits=8"))
assert any(m.endswith("_blasted") for m in memory_regions("flops", "auto", "pass.abc.memory_max_bits=8"))
assert not any(m.endswith("_blasted") for m in memory_regions("macro", "true"))
assert memory_reasons[0].startswith("auto: 64 bits > memory_max_bits"), memory_reasons
assert memory_reasons[1] == "pin synth.memory=flops" and memory_reasons[2] == "pin synth.memory=macro", memory_reasons
nested = work / "nested.prp"
nested.write_text('''comb lane::[synth.color="inner"](a:U8,b:U8)->(y:U8) { y=(a+b)#[0..=7] }
pub comb nested::[timecheck=false,synth.color="outer"](a:U8,b:U8,c:U8)->(x:U8,y:U8) {
 x=lane(a=a,b=b).y
 y=(a-c)#[0..=7]
}''')
_, nested_log = synth("nested", nested)
hit = [json.loads(l) for l in nested_log.splitlines() if '"synth-nested-group"' in l]
assert len(hit) == 1 and Path(hit[0]["span"]["file"]).name == "nested.prp" and hit[0]["span"]["start_line"] == 1, hit
# The timing target against what STA measured.
tight, tight_log = synth("nested", nested, "synth.opentimer=true", "pass.abc.delay=100")
tt = tight["qor"]["timing_target"]
assert tt["source"] == "abc.delay" and tt["met"] is False and tt["unreachable"] is True and tt["slack_ps"] < 0, tt
assert "synth-target-unreachable" in tight_log
loose, _ = synth("nested", nested, "synth.opentimer=true", "pass.abc.delay=100000")
tt = loose["qor"]["timing_target"]
assert tt["met"] is True and tt["unreachable"] is False and tt["slack_ps"] > 0, tt
# Attributes never change compilation (nicesynth C12): a callee carrying a pin is
# inlined exactly like an unattributed one, so a pin that restates the default
# gives the identical netlist (region bookkeeping may differ: a source pin
# anchors its cells, which are then shared as one implementation).
def inlined(name, head):
    path = work / (name + ".prp")
    path.write_text(head + "\npub comb " + name + "::[timecheck=false](a:U32,b:U32,n:U5)->(x:U32,z:U32) {\n"
                    " x=sh(a=a,n=n).y\n z=sh(a=b,n=n).y\n}")
    doc, _ = synth(name, path)
    total = doc["qor"]["abc"]["total"]
    return total["gates"], len(doc["qor"]["abc"]["regions"])
plain_sh = inlined("inl_plain", "comb sh(a:U32,n:U5)->(y:U32) { y=(a<<n)#[0..=31] }")
pinned_sh = inlined("inl_pinned", 'comb sh::[synth.barrel="log"](a:U32,n:U5)->(y:U32) { y=(a<<n)#[0..=31] }')
assert plain_sh[0] == pinned_sh[0], (plain_sh, pinned_sh)
print("region contraction A -> B -> A and module-scope isolation passed")
