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
# The replayed VALUES are asserted from the replay waveform (vcd_check.py below), parsed for signal changes rather than
# diffed against a golden, which would pin the writer's `$dumpvars` order and its wall-clock `$date` -- neither of
# which is content. The VCD is also required to EXIST and to carry both DUTs, since it is the artifact this test
# exists to protect.
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

# Shared VCD checker: values come from the replay waveform. A cycle's period is 10 time units; the value change an edge
# produces appears at that edge's timestamp, which is the NEXT cycle's "during period" sample, hence `cycle = t/10 + 1`.
cat > "$WORK/vcd_check.py" <<'PY'
import json, sys

def parse(path):
    names, scope, changes, order = {}, [], {}, []
    t = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("$scope"):
                scope.append(line.split()[2])
            elif line.startswith("$upscope"):
                scope.pop()
            elif line.startswith("$var"):
                p = line.split()
                names[p[3]] = ".".join(scope + [p[4].split("[")[0]])
            elif line.startswith("#"):
                t = int(line[1:])
                order.append(t)
            elif line.startswith("b"):
                v, i = line[1:].split()
                changes.setdefault(i, []).append((t, int(v, 2)))
            elif line and line[0] in "01xz" and len(line) > 1:
                changes.setdefault(line[1:], []).append((t, int(line[0]) if line[0] in "01" else -1))
    return names, changes, sorted(set(order))

def timeline(changes, ident, times):
    cur, out, j = None, {}, 0
    ch = changes.get(ident, [])
    for t in times:
        while j < len(ch) and ch[j][0] <= t:
            cur = ch[j][1]
            j += 1
        out[t] = cur
    return out

def divergence(vcd, witness):
    names, changes, times = parse(vcd)
    by = {n: i for i, n in names.items()}
    impl = timeline(changes, by["_lec_dut.impl_o"], times)
    ref = timeline(changes, by["_lec_dut.ref_o"], times)
    differ = [t for t in times if impl[t] != ref[t]]
    assert differ, "the two sides never part in the waveform"
    t = differ[0]
    assert all(impl[u] == ref[u] for u in times if u < t), "the sides must agree before they part"
    assert t // 10 + 1 == witness["diverge_cycle"], (t, witness["diverge_cycle"])
    return names, changes, times, impl, ref, t
PY

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
pub mod mini(`clock`:Clock, `reset`:Reset) -> (o:U8@[]) {
  reg cnt:U8:[reset_pin=`reset`] = 0
  o = cnt
  cnt = (cnt + 2)#[0..=7]
}
EOF

# The struct scenario below is an independent chain over its own files and
# workdirs (lec -> witness JSON -> replay sim, ~11s, nearly all of it the two
# host clang builds the replay needs). Start it now so it overlaps the mini
# chain; every assertion about it still runs in this shell, after the wait.
# `only_impl` is an impl-only input that drives NOTHING (a one-sided top input
# with no fanout is ignored); one that drove logic would be REFUTED-interface
# (exit 7, no counterexample, so no witness to replay).
cat > "$WORK/simpl.prp" <<'EOF'
pub mod dut(`clock`:Clock, `reset`:Reset, io:(valid:U1, bits:(x:U4, y:U3, only_impl:U2))) -> (o:U8@[]) {
  reg cnt:U8:[reset_pin=`reset`] = 0
  o = cnt
  const pc:U4 = io.bits.x#+[..]
  if io.valid != 0 { cnt = (cnt + pc + io.bits.y)#[0..=7] }
}
EOF
cat > "$WORK/sref.prp" <<'EOF'
pub mod dut(`clock`:Clock, `reset`:Reset, io:(valid:U1, bits:(x:U4, y:U3))) -> (o:U8@[]) {
  reg cnt:U8:[reset_pin=`reset`] = 0
  o = cnt
  const pc:U4 = io.bits.x#+[..]
  if io.valid != 0 { cnt = (cnt + pc + io.bits.y + 1)#[0..=7] }
}
EOF
WT="$WORK/wt"
rm -rf "$WT"
ST="$WORK/rerun_t"
rm -rf "$ST"
(
  $LHD lec --set formal.effort=proof --ref "$WORK/sref.prp" --impl "$WORK/simpl.prp" --workdir "$WT" >"$WORK/out2.log" 2>&1
) &
struct_pid=$!

W="$WORK/w"
rm -rf "$W"
# sim.vcd_fake_delay=false also exercises the knob forwarding in the generator
# (only a short allowlist of sim.* reaches the child `lhd sim`).
OUT=$($LHD lec --set formal.effort=proof --ref "$WORK/mini.v" --impl "$WORK/mini_bug.prp" --workdir "$W" \
        --set sim.vcd_fake_delay=false 2>&1)

ck "REFUTED-cex"             'echo "$OUT" | grep -q "^lec: .* REFUTED-cex"'
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

# ---- the replayed VALUES, from the waveform -------------------------------------
# Re-running the emitted testbench BY ITSELF is the whole point of the artifact (edit, re-run, watch the divergence
# move), so it runs in a fresh workdir with nothing else passed, with the VCD on, and THAT waveform is what is checked.
S="$WORK/rerun"
rm -rf "$S"
$LHD sim "$W/simfail_mini.prp" --workdir "$S" --set sim.vcd=true >/dev/null 2>&1
RV=$(ls "$S"/*.vcd 2>/dev/null | head -1)
ck "standalone re-run wrote a waveform" '[ -n "$RV" ] && [ -s "$RV" ]'

cat > "$WORK/chk.py" <<'PY'
import json, sys
sys.path.insert(0, sys.argv[3])
from vcd_check import divergence
w = json.load(open(sys.argv[2]))
names, changes, times, impl, ref, t = divergence(sys.argv[1], w)
# Each output moves EXACTLY once off a value both sides shared (settled at 0), and the two sides part on ONE cycle with
# the values the witness names.
assert impl[t] == 2 and ref[t] == 1, (impl[t], ref[t])
for sig, new in (("_lec_dut.impl_o", 2), ("_lec_dut.ref_o", 1)):
    ident = next(i for i, n in names.items() if n == sig)
    first = [(u, v) for u, v in changes[ident] if v != 0]
    assert first and first[0] == (t, new), (sig, first[:2])
    assert all(v == 0 for u, v in changes[ident] if u < t), f"{sig}: must start settled at 0"
# The ROOT CUT the verdict named is the `cnt` flop inside each DUT: the replay lands the same two values there.
st = {n: changes[i] for i, n in names.items() if n.endswith(".cnt")}
vals = sorted(next(v for u, v in ch if u == t) for ch in st.values())
assert vals == [1, 2], f"root-cut flops must hold ref=1 impl=2, got {vals}"
# The sibling witness JSON tells the same story as the simulation.
assert w["kind"] == "simfail", w
assert w["root_cut"]["ref"] == "1" and w["root_cut"]["impl"] == "2", w["root_cut"]
assert w["root_cut"]["file"].endswith("mini_bug.prp") and w["root_cut"]["line"] > 0, w["root_cut"]
print("ok")
PY
ck "replay reproduces the counterexample (waveform + witness JSON agree)" \
   'python3 "$WORK/chk.py" "$RV" "$W/simfail_mini.json" "$WORK" | grep -q ok'
ck "lec-written waveform tells the same story" \
   'python3 "$WORK/chk.py" "$W/simfail_mini.vcd" "$W/simfail_mini.json" "$WORK" | grep -q ok'

# ---- the verdict text names the same divergence -------------------------------
ck "verdict names the divergence" 'echo "$OUT" | grep -q "o(ref=1 impl=2)"'

# ---- the same chain over the shape that actually broke: STRUCT ports +
# popcount ------------------------------------------------------------------
# A struct port is not writable from a test, so the wrapper flattens it to one
# scalar per leaf; and an importable Pyrope pair must not be round-tripped
# through the writer. Popcount is now supported by the writer; the directory
# checks below still enforce direct imports. The INPUTS are solver-chosen, so nothing is pinned to a
# golden: the assertion is the definition of a counterexample — the two agree at
# every cycle before the one the witness names, and part on it with exactly the
# values it names.
wait "$struct_pid" || true   # a REFUTED lec exits non-zero; the cks below judge it
OUT2=$(cat "$WORK/out2.log")
ck "struct: REFUTED-cex"      'echo "$OUT2" | grep -q "^lec: .* REFUTED-cex"'
ck "struct: wrote the prp"    '[ -f "$WT/simfail_dut.prp" ]'
ck "struct: wrote the vcd"    '[ -s "$WT/simfail_dut.vcd" ]'
ck "struct: no writer round trip" '[ ! -d "$WT/lecfail_impl_prp" ] && [ ! -d "$WT/lecfail_ref_prp" ]'

cat > "$WORK/chkt.py" <<'PY'
import json, sys
sys.path.insert(0, sys.argv[3])
from vcd_check import divergence
w = json.load(open(sys.argv[2]))
rc = w["root_cut"]
names, changes, times, impl, ref, t = divergence(sys.argv[1], w)
assert (str(ref[t]), str(impl[t])) == (rc["ref"], rc["impl"]), f"replay gave ref={ref[t]} impl={impl[t]}, witness says {rc}"
print("ok")
PY
ck "struct: replay reproduces the counterexample"    'python3 "$WORK/chkt.py" "$WT/simfail_dut.vcd" "$WT/simfail_dut.json" "$WORK" | grep -q ok'

if [ $fail -ne 0 ]; then echo "lec_witness_replay_test: FAILED"; exit 1; fi
echo "lec_witness_replay_test: PASSED"
exit 0
