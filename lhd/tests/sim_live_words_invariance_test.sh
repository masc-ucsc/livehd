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
# Every budget must pass with the same per-test end_digest, with and without
# dirty gating. The plan check guards the coverage: at live_words=1 the latch
# update and the flop it gates must sit in DIFFERENT colors, or this test no
# longer exercises the cross-color read.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
work="${TEST_TMPDIR:-/tmp/lhd_sim_live_words_invariance_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Same design as inou/prp/tests/sim/conditional_internal_icg_call.prp (kept
# inline so this test owns its fixture).
cat >"$work/lw_icg.prp" <<'EOF'
mod lw_icg_cell(clk:bool, en:bool) -> (g:bool@[]) {
  reg held:bool:[latch=true]
  if !clk {
    held = en
  }
  g = clk and held
}

mod lw_icg_leaf(gclk:bool, d:u8) -> (q:u8@[]) {
  reg acc:u8:[clock_pin=ref gclk] = 0
  acc = acc + d
  q = acc
}

mod lw_icg_wrapper(clk:bool, gate:bool, d:u8) -> (q:u8@[]) {
  wire gclk:bool = nil
  gclk = lw_icg_cell(clk=clk, en=gate)
  q = lw_icg_leaf(gclk=gclk, d=d)
}

pub mod lw_icg_top(clk:bool, active:bool, gate:bool, d:u8) -> (q:u8@[], ctl:u8@[]) {
  q = 0
  if active {
    q = lw_icg_wrapper(clk=clk, gate=gate, d=d)
  }

  reg every:u8:[clock_pin=ref clk] = 0
  every = every + d
  ctl = every
}

test lw_icg_top.gated_state_advances_at_every_budget {
  mut dut = lw_icg_top
  mut bad = 0
  mut ctl = 0
  tick 5 {
    dut.active = (clock <= 1) or (clock == 4)
    dut.gate = true
    dut.d = 1 << clock
    ctl = ctl + (1 << clock)
    step

    if dut.ctl != ctl { bad = bad + 1 }
    mut want:u8 = 0
    if clock == 0 { want = 1 }
    if clock == 1 { want = 3 }
    if clock == 4 { want = 19 }
    if dut.q != want { bad = bad + 1 }
  }
  assert(bad == 0, "ICG-gated state must advance identically at every live-word budget")
}
EOF

# Hierarchical ICG (the latch in a child cell, the gated flop in a sibling
# leaf) with CONSTANT data: only the gate's edges may advance the flop.
cat >"$work/lw_icg_const.prp" <<'EOF'
mod lwc_icg_cell(clk:bool, en:bool) -> (g:bool@[]) {
  reg held:bool:[latch=true]
  if !clk {
    held = en
  }
  g = clk and held
}

mod lwc_icg_leaf(gclk:bool, d:u8) -> (q:u8@[]) {
  reg acc:u8:[clock_pin=ref gclk] = 0
  acc = acc + d
  q = acc
}

pub mod lwc_icg_top(clk:bool, gate:bool, d:u8) -> (q:u8@[], ctl:u8@[]) {
  wire gclk:bool = nil
  gclk = lwc_icg_cell(clk=clk, en=gate)
  q = lwc_icg_leaf(gclk=gclk, d=d)

  reg every:u8:[clock_pin=ref clk] = 0
  every = every + d
  ctl = every
}

test lwc_icg_top.gate_edges_alone_advance_the_flop {
  mut dut = lwc_icg_top
  mut bad = 0
  tick 8 {
    dut.gate = (clock == 1) or (clock == 2) or (clock == 4) or (clock == 7)
    dut.d = 1
    step

    if dut.ctl != clock + 1 { bad = bad + 1 }
    mut want:u8 = 0
    if clock >= 1 { want = 1 }
    if clock >= 2 { want = 2 }
    if clock >= 4 { want = 3 }
    if clock >= 7 { want = 4 }
    if dut.q != want { bad = bad + 1 }
  }
  assert(bad == 0, "a gate edge under constant data must advance the gated flop at every budget")
}
EOF

# tests/sim/icg_enable_sampling.prp's flat shape: the data changes only while
# the enable is LOW, so the enable's rise is the only event that loads it.
cat >"$work/lw_icg_flat.prp" <<'EOF'
pub mod lwf_icg(clk:bool, ein:bool, d:u8) -> (q:u8@[], ctl:u8@[]) {
  reg enl:bool:[latch=true]
  if !clk {
    enl = ein
  }
  wire g:bool = nil
  g = clk and enl
  reg f:u8:[clock_pin=ref g] = 0
  f = d
  q = f

  reg c:u8:[clock_pin=ref clk] = 0
  c   = d
  ctl = c
}

test lwf_icg.enable_edge_alone_loads {
  mut dut = lwf_icg
  mut bad = 0
  tick 8 {
    dut.ein = (clock == 2) or (clock == 4) or (clock == 5) or (clock == 7)
    mut dv:u8 = 5
    if (clock == 1) or (clock == 2) { dv = 7 }
    if (clock == 3) or (clock == 4) or (clock == 5) { dv = 9 }
    if (clock == 6) or (clock == 7) { dv = 3 }
    dut.d = dv
    step

    if dut.ctl != dv { bad = bad + 1 }
    mut want:u8 = 0
    if clock >= 2 { want = 7 }
    if clock >= 4 { want = 9 }
    if clock >= 7 { want = 3 }
    if dut.q != want { bad = bad + 1 }
  }
  assert(bad == 0, "the latched enable rising under unchanged data must load the flop at every budget")
}
EOF

# An ICG latch with a reset value (`= true`: reset opens the gate) and a gated
# flop WITHOUT a reset: the reset pulse at clock 4, with the enable low and the
# data constant, must advance the flop -- through the latch's reset `_din`.
cat >"$work/lw_icg_reset.prp" <<'EOF'
pub mod lwr_icg(clk:bool, ein:bool, d:u8) -> (q:u8@[]) {
  reg enl:bool:[latch=true] = true
  if !clk {
    enl = ein
  }
  wire g:bool = nil
  g = clk and enl
  reg f:u8:[clock_pin=ref g] = nil
  f = f + d
  q = f
}

test lwr_icg.reset_opens_the_gate_at_every_budget {
  mut dut = lwr_icg
  mut bad = 0
  tick 8 {
    dut.reset = clock == 4
    dut.ein = (clock == 1) or (clock == 6)
    dut.d = 1
    step

    mut want:u8 = 0
    if clock >= 1 { want = 1 }
    if clock >= 4 { want = 2 }
    if clock >= 6 { want = 3 }
    if dut.q != want { bad = bad + 1 }
  }
  assert(bad == 0, "the latch's reset value gates the flop identically at every budget")
}
EOF

run() {  # run <fixture> <tag> <extra --set args...>
  local fixture="$1"
  local tag="$2"
  shift 2
  local st=0
  "$LHD" sim "$work/$fixture.prp" --workdir "$work/$fixture/$tag" --set sim.tune.profile=off "$@" -q \
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
# flop whose commit guard reads the latch's `_din` must be in different colors.
split_guard() {  # split_guard <fixture> <root module> <tag>
  local plan
  plan="$(ls "$work/$1/$3"/sim/*"$2".color-plan.txt 2>/dev/null | head -1)"
  [ -f "$plan" ] || fail "$1: no color plan for the root at $3"
  python3 - "$plan" <<'PY' || fail "$1: $3 no longer splits the ICG latch from the flop it gates"
import sys

latches, flops = set(), set()
update_color = {}
edges = []
with open(sys.argv[1], encoding="utf-8") as stream:
    for line in stream:
        words = line.split()
        if not words:
            continue
        if words[0] == "site" and "kind=state" in words:
            (latches if "op=latch" in words else flops if "op=flop" in words else set()).add(words[1])
        elif words[0] == "version-site" and "role=state-update" in words:
            fields = dict(w.split("=", 1) for w in words[2:] if "=" in w)
            update_color[words[1]] = (fields["base"], fields["color"])
        elif words[0] == "version-edge":
            edges.append((words[1], words[3]))
split = [
    (producer, consumer)
    for producer, consumer in edges
    if producer in update_color and consumer in update_color
    and update_color[producer][0] in latches and update_color[consumer][0] in flops
    and update_color[producer][1] != update_color[consumer][1]
]
if not split:
    raise SystemExit("no latch-update -> flop-update edge crosses a color boundary")
PY
}

# All fixtures have distinct module/test names. Build them together once per
# tune vector instead of starting a compiler and host build for each fixture.
cat "$work/lw_icg.prp" "$work/lw_icg_const.prp" "$work/lw_icg_flat.prp" "$work/lw_icg_reset.prp" > "$work/lw_all.prp"
DIRTY_ON=(--set sim.tune.dirty=on --set sim.tune.fence=16)
# Keep power-on state deterministic for the constant-data fixtures.
FILL=(--set sim.unknown_zero=true --set sim.init_zero=true)
run lw_all lw256 "${FILL[@]}" --set sim.tune.live_words=256 --set sim.tune.dirty=off
run lw_all lw1 "${FILL[@]}" --set sim.tune.live_words=1 --set sim.tune.dirty=off
run lw_all lw2 "${FILL[@]}" --set sim.tune.live_words=2 --set sim.tune.dirty=off
run lw_all lw1_dirty "${FILL[@]}" --set sim.tune.live_words=1 "${DIRTY_ON[@]}"
run lw_all lw256_dirty "${FILL[@]}" --set sim.tune.live_words=256 "${DIRTY_ON[@]}"
compare lw_all lw256 lw1 lw2 lw1_dirty lw256_dirty
split_guard lw_all lw_icg_top lw1
for root in lwc_icg_top lwf_icg lwr_icg; do
  split_guard lw_all "$root" lw1_dirty
done
# Check every fixture's evaluator, so one fixture cannot mask a missing mark
# in another after batching the build.
for root in lwc_icg_top lwf_icg lwr_icg; do
  grep -q 'cross-color `_din` readers' "$work/lw_all/lw1_dirty"/sim/*"$root".cpp \
    || fail "$root: no cross-color \`_din\` dirty mark in the generated evaluator"
done

echo "PASS: ICG-gated state is invariant across sim.tune.live_words / dirty"
