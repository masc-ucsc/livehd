#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# sim.tune.* knobs are SPEED-ONLY: no tune vector may change a simulated value.
# sim.tune.live_words is the color coarsener's per-color live-word budget, so it
# decides which state updates share a color -- and nothing else may change.
# sim.tune.dirty=on skips a color whose inputs did not change, so every value a
# color reads must dirty it when it changes.
#
# Regression 1 (2026-09-18): an ICG latch's staged `_din` is read BY NAME by the
# gated flop's commit guard (the clock-gate fold). Inside one color the
# activation-domain analysis kept that latch update unconditional; at
# live_words=1 the latch and the flop landed in different colors, the latch
# kept its guarded `if (enable) { _din = ...; }` form, and the flop read a stale
# `_din` (the default-constructed 0 after reset_cycle, not the reset Q) --
# the flop never advanced and the test failed ONLY at live_words=1.
#
# Regression 2 (review round 2, codegen-fix:R2-1): that by-name `_din` read had
# no DIRTY edge. With sim.tune.dirty=on and the latch split from the flop it
# gates, a gate edge under CONSTANT data dirtied only the latch's color; the
# flop's color stayed idle and never committed, while dirty=off and a budget
# that fused the two colors both advanced it. The first fixture hid this
# because its data changes every cycle (`d = 1 << clock` dirties the flop's
# color through its data slot); the constant-data fixtures below do not.
#
# Regression 3 (found with regression 2): a latch with a reset value whose
# `_din` a SAME-color flop reads. The grouped reset slow path runs after every
# member of the color, so the fused flop sampled the pre-reset `_din` while a
# flop in a later color saw the reset value: an ICG latch with `= true` opened
# its gate during reset only when the coarsener split it from the flop --
# values depended on live_words even with dirty=off.
#
# (Regression 3's latch RESET value has no Verilog spelling -- a reset branch
# in an `always_latch` is data, not a latch reset -- so `lwr_icg` forces the
# latch open through its data path instead.)
#
# Every budget must pass with the same per-test end_digest, with and without
# dirty gating. The plan check guards the coverage: at live_words=1 the latch
# update and the flop it gates must sit in DIFFERENT colors, or this test no
# longer exercises the cross-color read.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
work="${TEST_TMPDIR:-/tmp/lhd_sim_live_words_invariance_$$}"
mkdir -p "$work"
part="${1:-all}"
case "$part" in
  all | 1 | 2 | 1_dirty | 256_dirty) ;;
  *) echo "FAIL: unknown live-word comparison $part" >&2; exit 1 ;;
esac

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The ICGs are hand-built (an enable latch transparent while the clock is low,
# ANDed with the clock -- minion's prim_clk_gate). Pyrope has no spelling for
# a latch gated by the clock (its one gate is the `Clock(clock_pin=, enable=)`
# Clock_cell, which has no latch), so the gate cells are imported Verilog and
# the conditional call that activates them stays Pyrope.
cat >"$work/lw_icg.v" <<'EOF'
module lw_icg_cell(input clk, input en, output g);
  reg held;
  always_latch if (!clk) held = en;
  assign g = clk & held;
endmodule

// Hierarchical ICG (the latch in a child cell, the gated flop in a sibling
// leaf) with CONSTANT data: only the gate's edges may advance the flop.
module lwc_icg_leaf(input gclk, input [7:0] d, output [7:0] q);
  reg [7:0] acc = 8'd0;
  always @(posedge gclk) acc <= acc + d;
  assign q = acc;
endmodule

module lwc_icg_top(input clk, input gate, input [7:0] d, output [7:0] q, output [7:0] ctl);
  wire gclk;
  lw_icg_cell u_cell(.clk(clk), .en(gate), .g(gclk));
  lwc_icg_leaf u_leaf(.gclk(gclk), .d(d), .q(q));
  reg [7:0] every = 8'd0;
  always @(posedge clk) every <= every + d;
  assign ctl = every;
endmodule

// tests/sim/icg_enable_sampling.prp's flat shape: the data changes only while
// the enable is LOW, so the enable's rise is the only event that loads it.
module lwf_icg(input clk, input ein, input [7:0] d, output [7:0] q, output [7:0] ctl);
  reg enl;
  always_latch if (!clk) enl = ein;
  wire g = clk & enl;
  reg [7:0] f = 8'd0;
  always @(posedge g) f <= d;
  assign q = f;
  reg [7:0] c = 8'd0;
  always @(posedge clk) c <= d;
  assign ctl = c;
endmodule

// A latch forced open (`ropen` opens the gate) and a gated flop: the pulse at
// cycle 4, with the enable low and the data constant, must advance the flop
// through the latch's `_din`.
module lwr_icg(input clk, input ropen, input ein, input [7:0] d, output [7:0] q);
  reg enl;
  always_latch if (ropen) enl = 1'b1; else if (!clk) enl = ein;
  wire g = clk & enl;
  reg [7:0] f = 8'd0;
  always @(posedge g) f <= f + d;
  assign q = f;
endmodule
EOF

# The conditional fixture is the design of inou/prp/tests/sim/
# conditional_internal_icg_call.prp with the Verilog gate cell. Every test
# counts its own cycles (`i`).
cat >"$work/lw_all.prp" <<'EOF'
const lw_icg_cell = import("lg:lw_icg_cell")
const lwc_icg_top = import("lg:lwc_icg_top")
const lwf_icg     = import("lg:lwf_icg")
const lwr_icg     = import("lg:lwr_icg")

mod lw_icg_leaf(gclk:Clock, d:U8) -> (q:U8@[]) {
  reg acc:U8:[clock_pin=gclk] = 0
  wrap acc = acc + d
  q = acc
}

mod lw_icg_wrapper(clk:Clock, gate:Bool, d:U8) -> (q:U8@[]) {
  const gclk = lw_icg_cell(clk=clk, en=gate)
  q = lw_icg_leaf(gclk=gclk, d=d)
}

pub mod lw_icg_top(clk:Clock, active:Bool, gate:Bool, d:U8) -> (q:U8@[], ctl:U8@[]) {
  q = 0
  if active {
    q = lw_icg_wrapper(clk=clk, gate=gate, d=d)
  }

  reg every:U8 = 0
  wrap every = every + d
  ctl = every
}

test lw_icg_top.gated_state_advances_at_every_budget {
  mut dut    = lw_icg_top
  mut bad    = 0
  mut ctl:U8 = 0
  mut i:U8   = 0
  tick 5 {
    dut.active = (i <= 1) or (i == 4)
    dut.gate = true
    dut.d = 1 << i
    ctl = ctl + (1 << i)
    step

    if dut.ctl != ctl { bad = bad + 1 }
    mut want:U8 = 0
    if i == 0 { want = 1 }
    if i == 1 { want = 3 }
    if i == 4 { want = 19 }
    if dut.q != want { bad = bad + 1 }
    i += 1
  }
  assert(bad == 0, "ICG-gated state must advance identically at every live-word budget")
}

test lwc_icg_top.gate_edges_alone_advance_the_flop {
  mut dut  = lwc_icg_top
  mut bad  = 0
  mut i:U8 = 0
  tick 8 {
    dut.gate = (i == 1) or (i == 2) or (i == 4) or (i == 7)
    dut.d = 1
    step

    if dut.ctl != i + 1 { bad = bad + 1 }
    mut want:U8 = 0
    if i >= 1 { want = 1 }
    if i >= 2 { want = 2 }
    if i >= 4 { want = 3 }
    if i >= 7 { want = 4 }
    if dut.q != want { bad = bad + 1 }
    i += 1
  }
  assert(bad == 0, "a gate edge under constant data must advance the gated flop at every budget")
}

test lwf_icg.enable_edge_alone_loads {
  mut dut  = lwf_icg
  mut bad  = 0
  mut i:U8 = 0
  tick 8 {
    dut.ein = (i == 2) or (i == 4) or (i == 5) or (i == 7)
    mut dv:U8 = 5
    if (i == 1) or (i == 2) { dv = 7 }
    if (i == 3) or (i == 4) or (i == 5) { dv = 9 }
    if (i == 6) or (i == 7) { dv = 3 }
    dut.d = dv
    step

    if dut.ctl != dv { bad = bad + 1 }
    mut want:U8 = 0
    if i >= 2 { want = 7 }
    if i >= 4 { want = 9 }
    if i >= 7 { want = 3 }
    if dut.q != want { bad = bad + 1 }
    i += 1
  }
  assert(bad == 0, "the latched enable rising under unchanged data must load the flop at every budget")
}

test lwr_icg.reset_opens_the_gate_at_every_budget {
  mut dut  = lwr_icg
  mut bad  = 0
  mut i:U8 = 0
  tick 8 {
    dut.ropen = i == 4
    dut.ein = (i == 1) or (i == 6)
    dut.d = 1
    step

    mut want:U8 = 0
    if i >= 1 { want = 1 }
    if i >= 4 { want = 2 }
    if i >= 6 { want = 3 }
    if dut.q != want { bad = bad + 1 }
    i += 1
  }
  assert(bad == 0, "the forced-open latch gates the flop identically at every budget")
}
EOF
"$LHD" compile "$work/lw_icg.v" --reader slang --emit-dir lg:"$work/lg/" --workdir "$work/lgw" -q >"$work/lg.log" 2>&1 || {
  cat "$work/lg.log" >&2
  fail "the Verilog ICG cells did not compile"
}

run() {  # run <fixture> <tag> <extra --set args...>
  local fixture="$1"
  local tag="$2"
  shift 2
  local st=0
  "$LHD" sim lg:"$work/lg" "$work/$fixture.prp" --workdir "$work/$fixture/$tag" --set sim.tune.profile=off "$@" -q \
    >"$work/$fixture.$tag.log" 2>&1 || st=$?
  [ "$st" -eq 0 ] || {
    cat "$work/$fixture.$tag.log" >&2
    fail "$fixture/$tag: lhd sim exited $st (the simulated values depend on the tune vector)"
  }
  [ -f "$work/$fixture/$tag/sim/sim_tests.json" ] || fail "$fixture/$tag: no sim_tests.json"
}

compare() {  # compare <fixture> <tag>...
  local fixture="$1"
  shift
  python3 - "$work/$fixture" "$@" <<'PY' || fail "$fixture: end_digest comparison failed"
import json
import os
import sys

work, tags = sys.argv[1], sys.argv[2:]
digests = {}
expected = {
    "lw_icg_top.gated_state_advances_at_every_budget",
    "lwc_icg_top.gate_edges_alone_advance_the_flop",
    "lwf_icg.enable_edge_alone_loads",
    "lwr_icg.reset_opens_the_gate_at_every_budget",
}
for tag in tags:
    with open(os.path.join(work, tag, "sim", "sim_tests.json"), encoding="utf-8") as stream:
        tests = json.load(stream)
    if len(tests) != len(expected) or {t["test"] for t in tests} != expected:
        raise SystemExit(f"{tag}: did not run all four ICG regressions")
    for test in tests:
        if test.get("status") != "pass":
            raise SystemExit(f"{tag}: {test.get('test')} status={test.get('status')}")
        digests.setdefault(test["test"], {})[tag] = test.get("end_digest")
for name, by_tag in digests.items():
    if len(by_tag) != len(tags) or len(set(by_tag.values())) != 1:
        raise SystemExit(f"{name}: end_digest differs across tune vectors: {by_tag}")
PY
}

# Coverage guard: at live_words=1 the ICG latch's update and the update of the
# flop it gates must be in different colors. Clock-window latches have a direct
# update edge; a phase-settled latch (the conditional fixture) reaches the flop
# through its clock-gate data cone instead.
split_guard() {  # split_guard <fixture> <root module> <tag>
  local plan
  plan="$(ls "$work/$1/$3"/sim/*"$2".color-plan.txt 2>/dev/null | head -1)"
  [ -f "$plan" ] || fail "$1: no color plan for the root at $3"
  python3 - "$plan" <<'PY' || fail "$1: $3 no longer splits the ICG latch from the flop it gates"
import sys

latches, flops = set(), set()
versions = {}
successors = {}
with open(sys.argv[1], encoding="utf-8") as stream:
    for line in stream:
        words = line.split()
        if not words:
            continue
        if words[0] == "site" and "kind=state" in words:
            (latches if "op=latch" in words else flops if "op=flop" in words else set()).add(words[1])
        elif words[0] == "version-site":
            fields = dict(w.split("=", 1) for w in words[2:] if "=" in w)
            versions[words[1]] = fields
        elif words[0] == "version-edge":
            successors.setdefault(words[1], []).append(words[3])
split = []
for producer, latch in versions.items():
    if latch["role"] != "state-update" or latch["base"] not in latches:
        continue
    pending = list(successors.get(producer, []))
    seen = set()
    while pending:
        consumer = pending.pop()
        if consumer in seen:
            continue
        seen.add(consumer)
        node = versions[consumer]
        if node["version"] != latch["version"]:
            continue
        if node["role"] == "state-update" and node["base"] in flops:
            if latch["color"] != node["color"]:
                split.append((producer, consumer))
        elif node["role"] == "data":
            # Follow only combinational logic, never another state element.
            pending.extend(successors.get(consumer, []))
if not split:
    raise SystemExit("no latch-update -> flop-update path crosses a color boundary")
PY
}

# All fixtures share one testbench (distinct module/test names): one compiler
# and host build per tune vector.
DIRTY_ON=(--set sim.tune.dirty=on --set sim.tune.fence=16)
# Keep power-on state deterministic for the constant-data fixtures.
FILL=(--set sim.unknown_zero=true --set sim.init_zero=true)
run lw_all lw256 "${FILL[@]}" --set sim.tune.live_words=256 --set sim.tune.dirty=off
tags=(lw256)
if [ "$part" = all ] || [ "$part" = 1 ]; then
  run lw_all lw1 "${FILL[@]}" --set sim.tune.live_words=1 --set sim.tune.dirty=off
  tags+=(lw1)
  split_guard lw_all lw_icg_top lw1
fi
if [ "$part" = all ] || [ "$part" = 2 ]; then
  run lw_all lw2 "${FILL[@]}" --set sim.tune.live_words=2 --set sim.tune.dirty=off
  tags+=(lw2)
fi
if [ "$part" = all ] || [ "$part" = 1_dirty ]; then
  run lw_all lw1_dirty "${FILL[@]}" --set sim.tune.live_words=1 "${DIRTY_ON[@]}"
  tags+=(lw1_dirty)
  # Check every fixture, so batching cannot hide a missing cross-color mark.
  for root in lwc_icg_top lwf_icg lwr_icg; do
    split_guard lw_all "$root" lw1_dirty
    grep -q 'cross-color `_din` readers' "$work/lw_all/lw1_dirty"/sim/*"$root".cpp \
      || fail "$root: no cross-color dirty mark in the generated evaluator"
  done
fi
if [ "$part" = all ] || [ "$part" = 256_dirty ]; then
  run lw_all lw256_dirty "${FILL[@]}" --set sim.tune.live_words=256 "${DIRTY_ON[@]}"
  tags+=(lw256_dirty)
fi
compare lw_all "${tags[@]}"
echo "PASS: ICG-gated state is invariant across sim.tune.live_words / dirty ($part)"
