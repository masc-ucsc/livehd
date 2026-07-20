#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# LIVE test for todo/livehd/2f-latch M4 — clock awareness in `lhd lec`. TWO of
# the three false-PROVEN holes are CLOSED (landed 2026-07-20); the third is
# tracked below with the reason it needs different machinery.
#
# Originally a fixme tracker for a LIVE SOUNDNESS BUG that is NOT about latches.
#
# `pass/lec/encode.cpp` never reads a Flop's `clock_pin` or `posclk` sinks. Its
# only transition function is
#     N = ITE(reset, initial, ITE(enable, din, Q))
# so the clock a flop is attached to, and the edge it triggers on, are invisible
# to the miter. Consequently `lhd lec` FALSELY PROVES pairs of designs that are
# plainly not equivalent. All three cases below were reproduced on 2026-07-20 and
# each returns PROVEN today; each MUST return REFUTED once M4 makes the encoder
# clock-aware.
#
#   1. gated vs ungated clock   — with en=0 the gated flop HOLDS, the ungated
#                                 flop LOADS d every cycle.
#   2. negedge vs posedge       — the two sample d on opposite edges.
#   3. two clocks vs one clock  — a 2-stage pipeline whose stages are driven by
#                                 independent clocks is not the same machine as
#                                 one where both stages share a clock.
#
# Why this is filed under the latch roadmap: an ICG is `latch + gated clock`, so
# any latch support that ignores the gate ships the most dangerous bug in the
# area (see also the sim half: a gated clock is computed as DEAD CODE and the
# flop advances every cycle anyway). Fixing the encoder is a prerequisite for
# latch commit classes, not a follow-up.
#
# NOTE ON EXPECTED CHURN: making the encoder clock-aware WILL change existing
# verdicts — that is the point. Re-baselining the minion full-design LEC is part
# of M4.
#
# Tagged `fixme` so `bazel test //...` stays green. Run it with:
#   bazel test //lhd/tests:lec_clock_blindness_test --test_tag_filters=

set -u

LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# Build an lgraph from a Verilog source via the slang reader.
build() {
  local name="$1"
  "$LHD" compile "$W/$name.v" --reader slang --top dut --emit-dir "lg:$W/lg_$name" \
    -q >"$W/$name.log" 2>&1 || fail "compile $name: $(cat "$W/$name.log")"
}

# Assert that two lgraphs REFUTE. Requiring a verbatim REFUTED (not merely
# "not PROVEN") matters: an M4 encoder that merely DEGRADED these pairs to
# UNKNOWN would satisfy a weaker check while the M4 gate demands REFUTED —
# inconclusive must keep this tracker red.
expect_refuted() {
  local impl="$1" ref="$2" what="$3"
  local out
  out="$("$LHD" lec --impl "lg:$W/lg_$impl" --ref "lg:$W/lg_$ref" --top dut \
          --workdir "$W/lec_${impl}_${ref}" 2>&1)"
  # A verdict MUST be present. An empty/errored run must never read as success —
  # that is exactly the vacuous-gate failure mode this roadmap warns about.
  if ! echo "$out" | grep -qE "'dut' (PROVEN|REFUTED|UNKNOWN)"; then
    fail "$what: no verdict in lec output (run failed?): $out"
  fi
  if echo "$out" | grep -qE "'dut' PROVEN"; then
    fail "$what: LEC returned PROVEN for two NON-equivalent designs (clock blindness)"
  fi
  if ! echo "$out" | grep -qE "'dut' REFUTED"; then
    fail "$what: verdict is inconclusive (UNKNOWN) — the M4 gate demands a REFUTED"
  fi
  echo "ok: $what REFUTED"
}

# ---------------------------------------------------------------- case 1: gate
cat > "$W/gated.v" <<'EOF'
module dut(input clk, input en, input [7:0] d, output reg [7:0] q);
  wire gclk = clk & en;
  always @(posedge gclk) q <= d;
endmodule
EOF
cat > "$W/ungated.v" <<'EOF'
module dut(input clk, input en, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
EOF

# ------------------------------------------------------------- case 2: edge
cat > "$W/negedge.v" <<'EOF'
module dut(input clk, input [7:0] d, output reg [7:0] q);
  always @(negedge clk) q <= d;
endmodule
EOF
cat > "$W/posedge.v" <<'EOF'
module dut(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
EOF

# ------------------------------------------------------------ case 3: domains
cat > "$W/twoclk.v" <<'EOF'
module dut(input clk, input clk2, input [7:0] d, output reg [7:0] q);
  reg [7:0] r;
  always @(posedge clk)  r <= d;
  always @(posedge clk2) q <= r;
endmodule
EOF
cat > "$W/oneclk.v" <<'EOF'
module dut(input clk, input clk2, input [7:0] d, output reg [7:0] q);
  reg [7:0] r;
  always @(posedge clk) r <= d;
  always @(posedge clk) q <= r;
endmodule
EOF

for m in gated ungated negedge posedge twoclk oneclk; do
  build "$m"
done

expect_refuted gated   ungated  "gated vs ungated clock"
expect_refuted twoclk  oneclk   "two clock domains vs one"

# ---- STILL BLIND: negedge vs posedge on the SAME clock -----------------------
# Deliberately NOT asserted as REFUTED, because it cannot be fixed by the
# mechanism the two cases above use. Gating a commit needs a condition that
# differs between the two designs, and with one shared clock a posedge and a
# negedge flop each commit exactly once per step — they differ only in WHERE in
# the step, which a step-granular relational encoding cannot express.
#
# Distinguishing them needs SUB-CYCLE resolution: a rise phase then a fall
# phase, with the negedge side reading the POST-RISE values (the two-phase tick
# `lhd sim` got in M5, pinned there by tests/sim/flop_sim_posneg.prp). In this
# encoder that means encoding each step TWICE, with the posedge Qs re-seeded to
# their next-state before the second pass.
#
# The assertion below is the honest one: it must not CRASH or silently vanish —
# a verdict must be present — and today that verdict is PROVEN. When the
# two-phase encoder lands, flip this to expect_refuted.
out="$("$LHD" lec --impl "lg:$W/lg_negedge" --ref "lg:$W/lg_posedge" --top dut \
        --workdir "$W/lec_negpos" 2>&1)"
if ! echo "$out" | grep -qE "'dut' (PROVEN|REFUTED|UNKNOWN)"; then
  fail "negedge vs posedge: no verdict in lec output (run failed?)"
fi
if echo "$out" | grep -qE "'dut' REFUTED"; then
  echo "ok: negedge vs posedge now REFUTED — promote this case to expect_refuted"
else
  echo "note: negedge vs posedge is still PROVEN (needs the two-phase encoder; see above)"
fi

echo "PASS: lec is clock-aware for gate + domain differences (edge phase still open)"
