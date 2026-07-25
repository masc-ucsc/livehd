#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# LIVE test for todo/livehd/2f-latch — clock awareness in `lhd lec`. ALL THREE
# false-PROVEN holes are now CLOSED: the ICG-cone and two-clock-net cases at M4
# (2026-07-20), the same-clock EDGE case at M8 (2026-07-24, edge normalization).
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
# LIVE (untagged): all three cases assert a REFUTED verdict.

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

# ---- case 2: negedge vs posedge on the SAME clock ---------------------------
# The hardest of the three, and the one M4 could NOT close. Gating a commit
# needs a condition that DIFFERS between the two designs, and with one shared
# clock a posedge and a negedge flop each commit exactly once per step — they
# differ only in WHERE in the step, which a step-granular relational encoding
# provably cannot express.
#
# M8 dissolves it instead of encoding it: `pass.single_edge` slots the negedge
# flop into its own sub-step, turning an INTRA-step ordering difference into an
# ordinary CROSS-step one that the existing Flop encoding already handles. The
# pass runs MITER-WIDE, so the all-posedge side is lowered into the same
# 2-slot time base rather than being compared against a differently-clocked
# machine.
expect_refuted negedge posedge "negedge vs posedge on one clock"

# ---- case 2b: the inversion lives in the CLOCK CONE, not on the pin ---------
# `always @(posedge ~clk)` is the SAME machine as `always @(negedge clk)`, but
# it reaches the IR as a cone (And(Not(Get_mask(clk)), 1)) rather than as
# posclk=false. The commit-class analysis used to keep only the cone's ROOT and
# take the edge from the pin alone, so the inversion was invisible and the whole
# negedge story was SPELLING-DEPENDENT -- wrong in BOTH directions at once:
# falsely PROVEN against a real posedge design, and falsely REFUTED against the
# identical negedge design (because only one side got lowered into a 2-slot time
# base). The yosys reader hides it -- its `proc` folds `~clk` into
# CLK_POLARITY=0 before we see a cone -- so this MUST stay on the slang reader,
# which is what `build` uses.
cat > "$W/nclk.v" <<'EOF'
module dut(input clk, input [7:0] d, output reg [7:0] q);
  wire nclk = ~clk;
  always @(posedge nclk) q <= d;
endmodule
EOF
build nclk
expect_refuted nclk posedge "posedge ~clk vs posedge clk (inversion in the clock CONE)"

out="$("$LHD" lec --impl "lg:$W/lg_nclk" --ref "lg:$W/lg_negedge" --top dut \
        --workdir "$W/lec_nclk_neg" 2>&1)"
if ! grep -qE "'dut' PROVEN" <<<"$out"; then
  echo "$out" | tail -5
  fail "posedge ~clk did NOT prove equal to negedge clk -- they are the same machine; the cone spelling is being lowered into a different time base than the pin spelling"
fi
echo "ok: posedge ~clk PROVES equal to negedge clk (the two spellings agree)"

# ---- VACUITY GUARD ----------------------------------------------------------
# Three REFUTEDs prove nothing on their own: an encoder that refuses everything,
# or one whose normalization is simply wrong, also refutes everything. Pin the
# other direction with a pair that is genuinely EQUIVALENT and still mixes
# edges — structurally different (so the structural-identity shortcut cannot
# answer it) and semantically the same. It must PROVE.
cat > "$W/mix_a.v" <<'EOF'
module dut(input clk, input [7:0] d, output reg [7:0] q);
  reg [7:0] a;
  always @(posedge clk) a <= d + 8'd3;
  always @(negedge clk) q <= a;
endmodule
EOF
cat > "$W/mix_b.v" <<'EOF'
module dut(input clk, input [7:0] d, output reg [7:0] q);
  reg [7:0] a;
  always @(posedge clk) a <= (d + 8'd5) - 8'd2;
  always @(negedge clk) q <= a;
endmodule
EOF
build mix_a
build mix_b
out="$("$LHD" lec --impl "lg:$W/lg_mix_a" --ref "lg:$W/lg_mix_b" --top dut \
        --workdir "$W/lec_mix" 2>&1)"
if ! echo "$out" | grep -qE "'dut' PROVEN"; then
  echo "$out" | tail -5
  fail "an EQUIVALENT mixed posedge/negedge pair did not PROVE — the three REFUTEDs above are vacuous"
fi
echo "ok: an equivalent mixed pos/neg pair still PROVES (the refutations are not vacuous)"

echo "PASS: lec is clock-aware for gate, domain AND edge differences"
