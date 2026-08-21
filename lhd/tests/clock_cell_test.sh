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
  grep -qa "PROVEN equivalent" <<<"$1" \
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
# 4. Cgen-style width wrappers remain timing-only all the way to clock_pin.
# ---------------------------------------------------------------------------
# A Verilog round trip can represent a one-bit clock through a signed widening
# pad: Clock_cell -> Get_mask/Sext -> Or(0, clock) -> Get_mask -> clock_pin. The
# Clock_cell intentionally has no DATA value, so every identity wrapper on that
# route must stay timing-only. Stopping at the OR made the next Get_mask wait for
# a value forever (Minion intpipe_csr_file: "all operands resolved yet never
# encoded"). A real two-input OR is not an identity and remains a derived clock.
cat > "$W/icg_pad.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module dut(input clk, input en, input [7:0] d, output [7:0] o);
  logic gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  wire signed [1:0] gclk_pad = $signed(2'b0) | $signed(gclk);
  wire gclk_used = gclk_pad[0];
  logic [7:0] f;
  always @(posedge gclk_used) f <= d;
  assign o = f;
endmodule
EOF
build icg_pad "$W/icg_pad.v" dut
sed 's/assign o = f;/assign o = ~(~f);/' "$W/icg_pad.v" > "$W/icg_pad_dm.v"
build icg_pad_dm "$W/icg_pad_dm.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icg_pad_dm" --ref "lg:$W/lg_icg_pad" --top dut --workdir "$W/l4" 2>&1)"
expect_proven "$out" "case 4 (Clock_cell behind signed zero-pad OR)"
grep -qa "Clock_cell" <<<"$out" \
  || { tail -8 <<<"$out"; fail "case 4 proved without recognizing the padded clock as a Clock_cell"; }
echo "ok: Clock_cell survives a cgen-style signed zero-pad OR"

out="$("$LHD" lec --impl "lg:$W/lg_icg_ungated" --ref "lg:$W/lg_icg_pad" --top dut --workdir "$W/l4b" 2>&1)"
expect_refuted "$out" "case 4b (padded gated vs ungated clock)"
echo "ok: the padded clock still gates -- gated vs ungated REFUTES"

# ---------------------------------------------------------------------------
# 5. A nested gate: the outer latch's gate is timing, never Clock_cell data.
# ---------------------------------------------------------------------------
# A second ICG driven by the first has `always_latch if (!gclk)`: after the
# inner gate is recognized, tolg's Boolean mux for `!gclk` has a Clock_cell as
# its selector. That mux only controls a clock-role latch window, which the
# phase schedule already absorbs. Trying to encode it as ordinary data refuses
# because Clock_cell intentionally has no sampled DATA value (Minion
# intpipe_csr_file's remaining round-trip blocker).
cat > "$W/icg_nested.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module dut(input clk, input en0, input en1, input [7:0] d, output [7:0] o);
  logic gclk0, gclk1;
  clkgate u_cg0(.clk_i(clk),   .en_i(en0), .clk_o(gclk0));
  clkgate u_cg1(.clk_i(gclk0), .en_i(en1), .clk_o(gclk1));
  logic [7:0] f;
  always @(posedge gclk1) f <= d;
  assign o = f;
endmodule
EOF
build icg_nested "$W/icg_nested.v" dut
sed 's/assign o = f;/assign o = ~(~f);/' "$W/icg_nested.v" > "$W/icg_nested_dm.v"
build icg_nested_dm "$W/icg_nested_dm.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icg_nested_dm" --ref "lg:$W/lg_icg_nested" --top dut --workdir "$W/l5" 2>&1)"
expect_proven "$out" "case 5 (nested Clock_cell timing latch)"
grep -qa "Clock_cell" <<<"$out" \
  || { tail -8 <<<"$out"; fail "case 5 proved without recognizing the nested Clock_cell chain"; }
echo "ok: a nested ICG latch consumes the inner Clock_cell as timing, not data"

# Cgen normalizes a generated clock's polarity with Boolean Eq/Xor nodes before
# wiring it to clock_pin. Re-read that Verilog so this test pins the exact path
# seen in Minion's generated intpipe_csr_file, not only the simpler source graph.
"$LHD" compile "lg:$W/lg_icg_nested" --top dut --recipe O0 \
  --emit "verilog:$W/icg_nested_cgen.v" --workdir "$W/cw_icg_nested_cgen" \
  >"$W/c_icg_nested_cgen.log" 2>&1 \
  || { tail -5 "$W/c_icg_nested_cgen.log"; fail "cgen round trip of nested gate failed"; }
build icg_nested_cgen "$W/icg_nested_cgen.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icg_nested_cgen" --ref "lg:$W/lg_icg_nested" --top dut --workdir "$W/l5rt" 2>&1)"
expect_proven "$out" "case 5 round trip (Clock_cell through cgen Eq/Xor clock_pin path)"
echo "ok: the cgen Eq/Xor clock_pin round trip remains timing-only and proves"

# Both enables must survive. Removing the outer gate must change behavior.
cat > "$W/icg_inner_only.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module dut(input clk, input en0, input en1, input [7:0] d, output [7:0] o);
  logic gclk0;
  clkgate u_cg0(.clk_i(clk), .en_i(en0), .clk_o(gclk0));
  logic [7:0] f;
  always @(posedge gclk0) f <= d;
  assign o = f;
endmodule
EOF
build icg_inner_only "$W/icg_inner_only.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icg_inner_only" --ref "lg:$W/lg_icg_nested" --top dut --workdir "$W/l5b" 2>&1)"
expect_refuted "$out" "case 5b (nested gate vs inner gate only)"
echo "ok: removing the outer gate REFUTES -- nested timing absorption preserves its enable"

# ---------------------------------------------------------------------------
# 6. A gate on a MEMORY clock -- the half the M8 fold structurally cannot reach.
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
out="$("$LHD" lec --impl "lg:$W/lg_icgm_dm" --ref "lg:$W/lg_icgm" --top dut --workdir "$W/l6" 2>&1)"
expect_proven "$out" "case 6 (gated memory, equivalent pair)"
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
out="$("$LHD" lec --impl "lg:$W/lg_icgm_ungated" --ref "lg:$W/lg_icgm" --top dut --workdir "$W/l6b" 2>&1)"
expect_refuted "$out" "case 6b (gated vs ungated MEMORY write)"
echo "ok: gated vs ungated memory writes REFUTE -- writes really are suppressed when the gate is closed"

# ---------------------------------------------------------------------------
# 7. A gated clock read as data by a resettable two-latch write primitive.
# ---------------------------------------------------------------------------
# This is Minion's exact prim_write_commit_rst_en structure. q is DATA-role:
# reset can open it independently of the clock, while its ordinary write arm is
# open only during the gated clock's high phase. A phase-aware encoder knows the
# Clock_cell level at each microstep and must preserve both reset and enable.
cat > "$W/icg_write_commit.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module write_commit(input clk_i, input rst_i, input en_i, input [7:0] d_i, output logic [7:0] q_o);
  logic en_1p;
  always_latch if (!clk_i) en_1p <= en_i;
  always_latch begin
    if (rst_i) q_o <= '0;
    else if (clk_i && en_1p) q_o <= d_i;
  end
endmodule
module dut(input clk, input gate, input rst, input en, input [7:0] d, output [7:0] o);
  logic gclk;
  logic [7:0] q;
  clkgate u_cg(.clk_i(clk), .en_i(gate), .clk_o(gclk));
  write_commit u_wc(.clk_i(gclk), .rst_i(rst), .en_i(en), .d_i(d), .q_o(q));
  assign o = q;
endmodule
EOF
build icg_write_commit "$W/icg_write_commit.v" dut
sed 's/assign o = q;/assign o = ~(~q);/' "$W/icg_write_commit.v" > "$W/icg_write_commit_dm.v"
build icg_write_commit_dm "$W/icg_write_commit_dm.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icg_write_commit_dm" --ref "lg:$W/lg_icg_write_commit" --top dut --workdir "$W/l7" 2>&1)"
expect_proven "$out" "case 7 (Clock_cell level in resettable write-commit latch)"
echo "ok: a resettable write-commit latch reads the scheduled Clock_cell level and proves"

# The phase-local clock value must still carry the gate. Removing it changes
# which high windows can write q and therefore must refute.
sed 's/write_commit u_wc(.clk_i(gclk)/write_commit u_wc(.clk_i(clk)/' "$W/icg_write_commit.v" > "$W/icg_write_commit_ungated.v"
build icg_write_commit_ungated "$W/icg_write_commit_ungated.v" dut
out="$("$LHD" lec --impl "lg:$W/lg_icg_write_commit_ungated" --ref "lg:$W/lg_icg_write_commit" --top dut --workdir "$W/l7b" 2>&1)"
expect_refuted "$out" "case 7b (gated vs ungated write-commit latch)"
echo "ok: removing the write-commit gate REFUTES -- the phase-local Clock_cell value retains its enable"

# ---------------------------------------------------------------------------
# 8. A Clock_cell feeding a COLLAPSED child's structural clock input.
# ---------------------------------------------------------------------------
# Top-down LEC proves a child once, then boxes it in the parent as a sequence
# transducer: equal input sequences justify sharing its output sequence.  The
# clock input is part of that contract.  A Clock_cell is intentionally
# timing-only for ordinary data consumers, but the box boundary must still
# compare its actual waveform; otherwise the bbin obligation never encodes
# (XiangShan ExuBlock -> CSR).  Conversely, dropping the gate must still make
# the child clock sequences differ and REFUTE.
cat > "$W/icg_box.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module child(input clk, input [7:0] d, output logic [7:0] q);
  always @(posedge clk) q <= d;
endmodule
module dut(input clk, input en, input [7:0] d, output [7:0] o);
  logic gclk;
  logic [7:0] q;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  child u_child(.clk(gclk), .d(d), .q(q));
  assign o = q;
endmodule
EOF
build icg_box "$W/icg_box.v" dut
sed 's/assign o = q;/assign o = ~(~q);/' "$W/icg_box.v" > "$W/icg_box_dm.v"
build icg_box_dm "$W/icg_box_dm.v" dut
sed 's/child u_child(.clk(gclk)/child u_child(.clk(clk)/' "$W/icg_box.v" > "$W/icg_box_ungated.v"
build icg_box_ungated "$W/icg_box_ungated.v" dut

out="$("$LHD" lec --impl "lg:$W/lg_icg_box_dm" --ref "lg:$W/lg_icg_box" --top dut \
        --workdir "$W/l8" --set formal.lec.hier=false --set formal.lec.collapse=child 2>&1)"
expect_proven "$out" "case 8 (Clock_cell into collapsed child clock port)"
echo "ok: a Clock_cell waveform is compared at a collapsed child's structural clock boundary"

out="$("$LHD" lec --impl "lg:$W/lg_icg_box_ungated" --ref "lg:$W/lg_icg_box" --top dut \
        --workdir "$W/l8b" --set formal.lec.hier=false --set formal.lec.collapse=child 2>&1)"
expect_refuted "$out" "case 8b (collapsed child gated vs ungated clock)"
echo "ok: a collapsed child's gated-vs-ungated clock REFUTES -- the boundary did not drop clock semantics"

echo "PASS"
