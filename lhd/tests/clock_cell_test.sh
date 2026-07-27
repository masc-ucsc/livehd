#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch M9 — `Clock_cell`, the ONE recognized clock operator.
#
# M8 folds a clock gate into a flop enable when the gate is VISIBLE IN THE BODY.
# Real designs INSTANTIATE the gate, so the flop's `clock_pin` is an opaque
# `Sub` output and nothing downstream can see it is a gate at all. M9 RECOGNIZES
# the instantiated cell at the `Sub` boundary and rewrites it into a first-class
# `Clock_cell(clk_ref, en, div=1, invert=false)`, which each consumer then lowers
# its own way (here: LEC, to a commit condition on the reference clock).
#
# WHAT MAKES RECOGNITION DIFFERENT FROM INLINING, and why this test exists
# alongside single_edge_hier_latch_test.sh case 5:
#
#   * NO def body is pulled into the compared cone. The enable LATCH never
#     crosses the boundary -- the cell encodes the glitch-free CONTRACT ("en is
#     sampled at clk_ref's active edge") instead of the implementation -- so a
#     design whose only latches were its clock gates stops holding latches at
#     all. Only the combinational enable CONE is re-rooted onto the instance's
#     own drivers, and that is a function of nets the parent already compares.
#   * It therefore works on a TRUSTED def (case 3), which inlining may never
#     touch. That is the whole reason minion's 30 encode refusals were stuck:
#     `prim_clk_gate` is on its trust list, so the inline path skipped it and
#     the gated flops kept refusing.
#
# The enable is a CONE, not a wire (case 2). minion's real gate latches
# `en_i | dft_i.scanmode` -- scan mode forces the clock ON -- so a recognizer
# that only accepts a bare input port as the enable matches nothing real.
#
# EVERY "it proves" case here is paired with a REFUTATION that must still fire,
# because a recognizer that silently dropped the gate would make the gated and
# ungated designs identical and prove everything.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

build() { # <name> <src> <top>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$2" --reader slang --top "$3" --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" \
    >"$W/c_$1.log" 2>&1 || { tail -5 "$W/c_$1.log"; fail "compile of $2 failed"; }
}

# A verdict must EXIST before we judge it: a build failure, an `unsupported`
# refusal before any encoding, or a crash matches neither "proven" nor
# "refuted", so a bare negative grep would print ok without lec ever running.
expect_proven() { # <output> <what>
  grep -qaiE "PROVEN|equivalent" <<<"$1" \
    || { tail -8 <<<"$1"; fail "$2: never reached a PROVEN verdict"; }
  grep -qaiE "refut|not equivalent|equiv_fail" <<<"$1" \
    && { tail -8 <<<"$1"; fail "$2: REFUTED two equivalent designs"; }
  return 0
}

expect_refuted() { # <output> <what>
  grep -qaiE "refut|not equivalent|equiv_fail" <<<"$1" \
    || { tail -8 <<<"$1"; fail "$2: expected a REFUTATION and did not get one -- the gate was dropped, or the miter is vacuous"; }
  return 0
}

# ---------------------------------------------------------------------------
# 1. An INSTANTIATED gate, one level down: recognized, and the gate still gates.
# ---------------------------------------------------------------------------
cat > "$W/icg.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module dut(input clk, input en, input [7:0] d, output [7:0] o);
  logic gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  logic [7:0] f;
  always @(posedge gclk) f <= d;
  assign o = f;
endmodule
EOF
build icg "$W/icg.v" dut

# Same design, De Morgan'd in the DATA path only: equivalent, must PROVE. This
# is the vacuity guard -- without it, a recognizer that refused everything or
# mangled every design would still pass case 1's refutation below.
sed 's/assign o = f;/assign o = ~(~f);/' "$W/icg.v" > "$W/icg_dm.v"
build icg_dm "$W/icg_dm.v" dut

out="$("$LHD" lec --impl "lg:$W/lg_icg_dm" --ref "lg:$W/lg_icg" --top dut --workdir "$W/l1" 2>&1)"
expect_proven "$out" "case 1 (instantiated gate, equivalent pair)"
# ...and it got there through RECOGNITION, not through the inline+fold path.
grep -qa "Clock_cell" <<<"$out" \
  || { tail -8 <<<"$out"; fail "case 1 proved, but no Clock_cell recognition step appears in the recipe -- it went through some other path, so this test is not pinning M9"; }
echo "ok: an instantiated clock gate one level down is recognized as a Clock_cell and proves"

# THE GATE MUST STILL GATE. Ungate the flop: if recognition dropped the enable,
# the two are identical and this PROVES instead of refuting.
#
# The gate instance is REMOVED, not merely bypassed. Leaving it in while nothing
# reads its output makes it an unused instance holding a latch: recognition only
# fires for a cell that actually drives a clock_pin (the binding rule), so the
# `Sub` survives and the def scan honestly refuses `clkgate holds 1 latch cell`
# — which is neither the proof nor the refutation this case is testing for.
cat > "$W/icg_ungated.v" <<'EOF'
module dut(input clk, input en, input [7:0] d, output [7:0] o);
  logic [7:0] f;
  always @(posedge clk) f <= d;
  assign o = f;
endmodule
EOF
build icg_ungated "$W/icg_ungated.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icg_ungated" --ref "lg:$W/lg_icg" --top dut --workdir "$W/l1b" 2>&1)"
expect_refuted "$out" "case 1b (gated vs ungated)"
echo "ok: gated vs ungated still REFUTES -- the recognized cell really gates"

# ---------------------------------------------------------------------------
# 2. The enable is a CONE of two ports, not a wire (minion's real shape).
# ---------------------------------------------------------------------------
# `en_i | scan` mirrors prim_clk_gate's `en_i | dft_i.scanmode`: scan mode forces
# the clock ON. A recognizer that only matched a bare port as the latched value
# would silently not fire here -- and "not recognized" is invisible except as a
# refusal, which is why this case asserts the recipe line as well as the verdict.
cat > "$W/icgc.v" <<'EOF'
module clkgate(input clk_i, input en_i, input scan_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i | scan_i;
  assign clk_o = clk_i & en_latch;
endmodule
module dut(input clk, input en, input scan, input [7:0] d, output [7:0] o);
  logic gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .scan_i(scan), .clk_o(gclk));
  logic [7:0] f;
  always @(posedge gclk) f <= d;
  assign o = f;
endmodule
EOF
build icgc "$W/icgc.v" dut
sed 's/assign o = f;/assign o = ~(~f);/' "$W/icgc.v" > "$W/icgc_dm.v"
build icgc_dm "$W/icgc_dm.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icgc_dm" --ref "lg:$W/lg_icgc" --top dut --workdir "$W/l2" 2>&1)"
expect_proven "$out" "case 2 (cone enable, equivalent pair)"
grep -qa "Clock_cell" <<<"$out" \
  || { tail -8 <<<"$out"; fail "case 2: a two-port enable CONE was not recognized -- the recognizer accepts only a bare port, which matches no real ICG"; }
echo "ok: an enable that is a CONE of two ports (the scan-enable shape) is recognized"

# The SCAN term must survive re-rooting. Drop it on one side only: scan mode no
# longer forces the clock on, so the two designs differ and this must refute.
# If the recognizer kept only `en_i`, both sides look the same and it proves.
sed 's/always_latch if (!clk_i) en_latch <= en_i | scan_i;/always_latch if (!clk_i) en_latch <= en_i;/' \
    "$W/icgc.v" > "$W/icgc_noscan.v"
build icgc_noscan "$W/icgc_noscan.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icgc_noscan" --ref "lg:$W/lg_icgc" --top dut --workdir "$W/l2b" 2>&1)"
expect_refuted "$out" "case 2b (scan term dropped from the enable cone)"
echo "ok: dropping the scan term from the enable cone REFUTES -- the whole cone is re-rooted, not just the first port"

# ---------------------------------------------------------------------------
# 3. THE M9 DIFFERENTIATOR: a TRUSTED gate is still recognized.
# ---------------------------------------------------------------------------
# Inlining may never touch a trusted def -- "assume these two equal, do not
# compare them" -- and single_edge_hier_latch_test case 5b pins that. So under
# the old inline-only path a trusted clock gate left every flop it clocked
# UNPROVEN. Recognition reads the def's SHAPE and re-roots a pure comb cone; no
# state and no new compare point crosses the boundary, so trust is respected
# and the gated flop still encodes. That is exactly what unblocks minion, whose
# `prim_clk_gate` is trusted.
out="$("$LHD" lec --impl "lg:$W/lg_icg_dm" --ref "lg:$W/lg_icg" --top dut \
        --workdir "$W/l3" --set formal.lec.trust=clkgate 2>&1)"
expect_proven "$out" "case 3 (trusted gate, equivalent pair)"
echo "ok: a TRUSTED clock gate is still recognized -- proving what inlining had to leave unproven"

# Trust must not become a way to lose the gate either.
out="$("$LHD" lec --impl "lg:$W/lg_icg_ungated" --ref "lg:$W/lg_icg" --top dut \
        --workdir "$W/l3b" --set formal.lec.trust=clkgate 2>&1)"
expect_refuted "$out" "case 3b (trusted gate, gated vs ungated)"
echo "ok: with the gate TRUSTED, gated vs ungated still REFUTES"

# ---------------------------------------------------------------------------
# 4. A gate on a MEMORY clock -- the half the M8 fold structurally cannot reach.
# ---------------------------------------------------------------------------
# The fold rewrites flop clocks into enables and never touches a Memory's
# clock_pin, so a gated memory was refused (or, before the guard, silently
# encoded as writing every step -- a measured false PROVEN). The cell reaches it.
cat > "$W/icgm.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module dut(input clk, input en, input we, input [3:0] a, input [7:0] d, output [7:0] o);
  logic gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  logic [7:0] mem [0:15];
  always @(posedge gclk) if (we) mem[a] <= d;
  assign o = mem[a];
endmodule
EOF
build icgm "$W/icgm.v" dut
sed 's/assign o = mem\[a\];/assign o = ~(~mem[a]);/' "$W/icgm.v" > "$W/icgm_dm.v"
build icgm_dm "$W/icgm_dm.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icgm_dm" --ref "lg:$W/lg_icgm" --top dut --workdir "$W/l4" 2>&1)"
expect_proven "$out" "case 4 (gated memory, equivalent pair)"
echo "ok: a gated MEMORY clock encodes -- the half the flop-enable fold cannot reach"

# The memory gate must gate: ungate the write clock and the two must differ.
# Standalone, for the same reason as case 1b: an unused gate instance keeps a
# latch the def scan would refuse on before either verdict is reached.
cat > "$W/icgm_ungated.v" <<'EOF'
module dut(input clk, input en, input we, input [3:0] a, input [7:0] d, output [7:0] o);
  logic [7:0] mem [0:15];
  always @(posedge clk) if (we) mem[a] <= d;
  assign o = mem[a];
endmodule
EOF
build icgm_ungated "$W/icgm_ungated.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icgm_ungated" --ref "lg:$W/lg_icgm" --top dut --workdir "$W/l4b" 2>&1)"
expect_refuted "$out" "case 4b (gated vs ungated MEMORY write)"
echo "ok: gated vs ungated memory writes REFUTE -- writes really are suppressed when the gate is closed"

echo "PASS"
