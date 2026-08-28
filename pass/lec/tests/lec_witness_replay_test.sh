#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end guard for the lec-on-failure REPRODUCTION artifacts: a REFUTED
# `lhd lec` must write a counterexample Pyrope testbench (formal.simfail), run it
# through `lhd sim` to a VCD (formal.simfail_run), and that testbench must replay
# exactly the divergence the verdict named.
#
# lec_witness_prpfail_test covers the .prp GENERATION hermetically (its sim leg
# stops at `--setup-only`). This one is the leg that was silently lost before:
# the whole chain can go quiet — an emit step refuses, a side stops parsing, the
# sim fails to build — and `lhd lec` still exits REFUTED with the right verdict,
# so nothing downstream notices that the reproduction stopped being produced.
#
# The replayed VALUES are asserted with `lhd sim --query` (docs/pyrope 09b) rather
# than by diffing the waveform: the query answers are structured, exact and
# full-width, while a VCD golden would pin the writer's `$dumpvars` hash order and
# its wall-clock `$date` — neither of which is content. The VCD is still required
# to EXIST and to carry both DUTs, since it is the artifact this test exists to
# protect.
#
# The design pair is chosen so the counterexample is UNIQUE and forced, which is
# what makes a pinned trace legitimate rather than flaky:
#   * the DUT has NO data inputs — only clock and reset, both pinned by the
#     harness — so the solver has no free bits to pick differently run to run;
#   * both sides reset ASYNCHRONOUSLY to 0, so no flop reaches the simulator
#     unreset and no power-on bit is drawn from the seeded PRNG;
#   * the two count by +1 and +2, so exactly ONE observable point diverges — and
#     `op:"changes"` returning count==1 from a common `old` on BOTH outputs is a
#     direct proof of that, not an assumption.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecvcd}"
mkdir -p "$WORK"
fail=0
ck() { if eval "$2"; then echo "ok: $1"; else echo "FAIL: $1"; fail=1; fi; }

# ref: +1 per cycle.  impl: +2 per cycle.  Async reset on both sides, no data in.
cat > "$WORK/mini.v" <<'EOF'
module mini(input clock, input reset, output [7:0] o);
  reg [7:0] cnt;
  always @(posedge clock or posedge reset) begin
    if (reset) cnt <= 8'd0;
    else       cnt <= cnt + 8'd1;
  end
  assign o = cnt;
endmodule
EOF
cat > "$WORK/mini_bug.prp" <<'EOF'
pub mod mini(clock:u1, reset:u1) -> (o:u8@[]) {
  reg cnt:u8:[reset_pin=ref reset] = 0
  o = cnt
  cnt = (cnt + 2)#[0..=7]
}
EOF

W="$WORK/w"
rm -rf "$W"
# sim.vcd_fake_delay=false also exercises the knob forwarding in the generator
# (only a short allowlist of sim.* reaches the child `lhd sim`).
OUT=$($LHD lec --ref "$WORK/mini.v" --impl "$WORK/mini_bug.prp" --workdir "$W" \
        --set sim.vcd_fake_delay=false 2>&1)

ck "REFUTED"                 'echo "$OUT" | grep -q REFUTED'
ck "wrote simfail_mini.prp"  '[ -f "$W/simfail_mini.prp" ]'
ck "wrote simfail_mini.json" '[ -f "$W/simfail_mini.json" ]'
ck "wrote simfail_mini.vcd"  '[ -s "$W/simfail_mini.vcd" ]'
ck "announced the waveform"  'echo "$OUT" | grep -q "wrote counterexample waveform"'
if [ ! -s "$W/simfail_mini.vcd" ]; then
  echo "$OUT" | tail -20
  echo "lec_witness_replay_test: FAILED (no VCD — the reproduction chain is broken)"
  exit 1
fi
# The waveform must carry BOTH designs, at the top and as nested instance scopes
# (a VCD of only one side would still be a file).
ck "VCD declares both DUT outputs" \
   'grep -q "impl_o\[7:0\]" "$W/simfail_mini.vcd" && grep -q "ref_o\[7:0\]" "$W/simfail_mini.vcd"'
ck "VCD carries both sub-instances" \
   '[ "$(grep -c "^\$scope module u_" "$W/simfail_mini.vcd")" = 2 ]'

# ---- the replayed VALUES, via `lhd sim --query` --------------------------------
# Re-running the emitted testbench BY ITSELF is the whole point of the artifact
# (edit, re-run, watch the divergence move), so the query runs against the .prp
# in a fresh workdir with nothing else passed.
cat > "$WORK/q.json" <<'EOF'
{"schema_version": 1, "kind": "sim_query", "queries": [
  {"id": "impl_changes", "op": "changes", "signal": "_lec_dut.impl_o", "from": {"cycle": 0}, "to": {"cycle": 3}},
  {"id": "ref_changes",  "op": "changes", "signal": "_lec_dut.ref_o",  "from": {"cycle": 0}, "to": {"cycle": 3}},
  {"id": "outs",         "op": "values",  "kind": "output", "at": {"cycle": 3}},
  {"id": "state",        "op": "values",  "kind": "flop",   "at": {"cycle": 2}}
]}
EOF
S="$WORK/rerun"
rm -rf "$S"
$LHD sim "$W/simfail_mini.prp" --workdir "$S" --result-json "$WORK/q.out.json" \
     --query "$WORK/q.json" >/dev/null 2>&1
ck "standalone re-run answered the query" '[ -s "$WORK/q.out.json" ]'

cat > "$WORK/chk.py" <<'PY'
import json, sys
q = json.load(open(sys.argv[1]))["query"]
r = {x["id"]: x for x in q["results"]}
for k, v in r.items():
    assert v.get("ok"), f"query {k} failed: {v}"

# Each output moves EXACTLY once, off a value both sides shared. That pins the
# whole trace of both outputs AND proves the failure point is unique: equal
# before the change, and diverging only at it.
exp = {"impl_changes": ("_lec_dut.impl_o", "2"), "ref_changes": ("_lec_dut.ref_o", "1")}
cyc = set()
for qid, (sig, new) in exp.items():
    ch = r[qid]["changes"]
    assert r[qid]["signal"] == sig, r[qid]
    assert len(ch) == 1, f"{qid}: expected ONE transition, got {ch}"
    assert ch[0]["old"]["dec"] == "0", f"{qid}: must start settled at 0, got {ch[0]['old']}"
    assert ch[0]["new"]["dec"] == new, f"{qid}: expected {new}, got {ch[0]['new']}"
    cyc.add(ch[0]["cycle"])
assert len(cyc) == 1, f"the two sides must part on ONE cycle, got {sorted(cyc)}"

# Same values read directly at the diverging cycle, full width.
outs = {v["signal"]: v["value"] for v in r["outs"]["values"]}
assert outs["_lec_dut.impl_o"]["dec"] == "2", outs
assert outs["_lec_dut.ref_o"]["dec"] == "1", outs
assert outs["_lec_dut.impl_o"]["hex"] == "02", outs   # ceil(8/4) digits, no truncation
assert outs["_lec_dut.ref_o"]["hex"] == "01", outs

# The ROOT CUT the verdict named is the `cnt` flop inside each DUT, and the replay
# must land the same two values there. A flop is sampled SETTLED end-of-period
# while an output reports what it drove DURING the period, so the state that the
# cycle-3 outputs inherit is the flop at cycle 2 — one edge earlier, by design
# (see the sampling note in the simquery docs; reading both at cycle 3 gives 4/2,
# the NEXT state, which looks like a bug and is not).
st = {s.rsplit(".", 1)[0]: v["dec"] for s, v in
      ((x["signal"], x["value"]) for x in r["state"]["values"]) if s.endswith(".cnt")}
assert sorted(st.values()) == ["1", "2"], f"root-cut flops must hold ref=1 impl=2, got {st}"
assert all(v["value"]["sampled"] == "settled" for v in r["state"]["values"]), r["state"]
assert all(v["value"]["sampled"] == "during_period" for v in r["outs"]["values"]), r["outs"]

# The sibling witness JSON must tell the same story as the simulation.
w = json.load(open(sys.argv[2]))
assert w["kind"] == "simfail", w
assert w["root_cut"]["ref"] == "1" and w["root_cut"]["impl"] == "2", w["root_cut"]
assert w["diverge_cycle"] == cyc.pop(), (w["diverge_cycle"], cyc)
assert w["root_cut"]["file"].endswith("mini_bug.prp") and w["root_cut"]["line"] > 0, w["root_cut"]
print("ok")
PY
ck "replay reproduces the counterexample (query + witness JSON agree)" \
   'python3 "$WORK/chk.py" "$WORK/q.out.json" "$W/simfail_mini.json" | grep -q ok'

# ---- the verdict text names the same divergence -------------------------------
ck "verdict names the divergence" 'echo "$OUT" | grep -q "o(ref=1 impl=2)"'

# ---- the same chain over the shape that actually broke: STRUCT ports + a
# construct pass.prp_writer cannot emit --------------------------------------
# A struct port is not writable from a test, so the wrapper flattens it to one
# scalar per leaf; and an importable Pyrope pair must not be round-tripped
# through the writer, or `popcount` (`#+[..]`) takes the whole reproduction with
# it. Here the counterexample INPUTS are solver-chosen, so nothing is pinned to a
# golden: the assertion is the definition of a counterexample — the two agree at
# every cycle before the one the witness names, and part on it with exactly the
# values it names.
cat > "$WORK/simpl.prp" <<'EOF'
pub mod dut(clock:u1, reset:u1, io:(valid:u1, bits:(x:u4, y:u3, only_impl:u2))) -> (o:u8@[]) {
  reg cnt:u8:[reset_pin=ref reset] = 0
  o = cnt
  const pc:u4 = io.bits.x#+[..]
  if io.valid { cnt = (cnt + pc + io.bits.y + io.bits.only_impl)#[0..=7] }
}
EOF
cat > "$WORK/sref.prp" <<'EOF'
pub mod dut(clock:u1, reset:u1, io:(valid:u1, bits:(x:u4, y:u3))) -> (o:u8@[]) {
  reg cnt:u8:[reset_pin=ref reset] = 0
  o = cnt
  const pc:u4 = io.bits.x#+[..]
  if io.valid { cnt = (cnt + pc + io.bits.y + 1)#[0..=7] }
}
EOF
WT="$WORK/wt"
rm -rf "$WT"
OUT2=$($LHD lec --ref "$WORK/sref.prp" --impl "$WORK/simpl.prp" --workdir "$WT" 2>&1)
ck "struct: REFUTED"          'echo "$OUT2" | grep -q REFUTED'
ck "struct: wrote the prp"    '[ -f "$WT/simfail_dut.prp" ]'
ck "struct: wrote the vcd"    '[ -s "$WT/simfail_dut.vcd" ]'
ck "struct: no writer round trip" '[ ! -d "$WT/lecfail_impl_prp" ] && [ ! -d "$WT/lecfail_ref_prp" ]'

# The query follows the witness JSON's diverge_cycle rather than a fixed number.
python3 - "$WT/simfail_dut.json" > "$WORK/qt.json" <<'PY'
import json, sys
n = json.load(open(sys.argv[1]))["diverge_cycle"]
print(json.dumps({"schema_version": 1, "kind": "sim_query",
                  "queries": [{"id": f"o{c}", "op": "values", "kind": "output",
                               "at": {"cycle": c}} for c in range(n + 1)]}))
PY
ST="$WORK/rerun_t"
rm -rf "$ST"
$LHD sim "$WORK/simpl.prp" "$WORK/sref.prp" "$WT/simfail_dut.prp" --workdir "$ST"      --result-json "$WORK/qt.out.json" --query "$WORK/qt.json" >/dev/null 2>&1
cat > "$WORK/chkt.py" <<'PY'
import json, sys
w  = json.load(open(sys.argv[2]))
n  = w["diverge_cycle"]
rc = w["root_cut"]
r  = {x["id"]: x for x in json.load(open(sys.argv[1]))["query"]["results"]}
for c in range(n + 1):
    q = r[f"o{c}"]
    assert q.get("ok"), q
    v = {x["signal"]: x["value"]["dec"] for x in q["values"]}
    impl, ref = v["_lec_dut.impl_o"], v["_lec_dut.ref_o"]
    if c < n:
        assert impl == ref, f"cycle {c}: the sides must still agree, got impl={impl} ref={ref}"
    else:
        assert impl != ref, f"cycle {n}: the sides must part, both read {impl}"
        assert (ref, impl) == (rc["ref"], rc["impl"]),             f"cycle {n}: replay gave ref={ref} impl={impl}, witness says {rc}"
print("ok")
PY
ck "struct: replay reproduces the counterexample"    'python3 "$WORK/chkt.py" "$WORK/qt.out.json" "$WT/simfail_dut.json" | grep -q ok'

if [ $fail -ne 0 ]; then echo "lec_witness_replay_test: FAILED"; exit 1; fi
echo "lec_witness_replay_test: PASSED"
exit 0
