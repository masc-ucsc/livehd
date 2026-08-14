#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch — CLOCK GATING, encoded as a FLOP ENABLE.
#
# An integrated clock gate is the commonest latch structure in real hardware:
#
#     always_latch  if (!clk) enl <= en;      // enable latch, opposite phase
#     wire gclk = clk & enl;                  // the gated clock
#     always @(posedge gclk) f <= d;          // the gated flop
#
# Modelling that as a gated CLOCK is both unsound and wasteful for formal. The
# encoder has no clock-identity model, so a gated clock and an ungated one look
# identical unless an ICG name heuristic happens to fire; and a gate costs a
# per-step commit term on every flop. But a gated clock IS a flop enable: the
# flop commits on the reference edge exactly when the enables hold there. So
# `pass.single_edge` recognizes the pattern and rewrites it:
#
#     always @(posedge clk) if (en) f <= d;
#
# which is what the encoder already models natively, at zero extra cost.
#
# THE ENABLE-LATCH BYPASS is what makes the rewrite exact, and it is the easiest
# thing to get wrong. A real ICG latches its enable on the opposite phase purely
# to suppress glitches: the latch CLOSES exactly when the flop SAMPLES, so the
# flop sees the value the latch was passing THROUGH, not its previously-held
# one. Using the latch's Q instead is a full cycle late — the classic L1 error,
# and it is invisible to a symmetric before/after gate because both sides would
# be wrong the same way. Hence the iverilog leg below, and its negative control.
#
# The resolution deliberately FAILS CLOSED when it cannot tell the clock operand
# from the enable: in the canonical `clk & en` BOTH are graph inputs, so "is it
# an input?" cannot decide, and guessing backwards would classify the enable as
# the clock and drop the gate entirely.

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

cat > "$W/icg.prp" <<'EOF'
pub mod icgf(clk:bool, en:bool, d:u8) -> (q:u8@[1]) {
  reg enl:bool:[latch=true]      // the enable latch, transparent on the LOW phase
  if !clk {
    enl = en
  }
  wire gclk:bool = nil
  gclk = clk and enl             // the gated clock
  reg f:u8:[clock_pin=ref gclk] = 0
  q = f
  f = d
}
EOF
# A REAL difference inside the same ICG structure: the gate must not mask it.
sed 's/  f = d/  f = d + 1/' "$W/icg.prp" > "$W/icg_bad.prp"

build() { # <name> <src>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$2" --top icgf --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" >"$W/c_$1.log" 2>&1 \
    || { tail -5 "$W/c_$1.log"; fail "compile of $2 failed"; }
}
build good "$W/icg.prp"
build bad  "$W/icg_bad.prp"

# ---- the pattern is RECOGNIZED and rewritten into an enable -----------------
rm -rf "$W/lg_norm"
out="$("$LHD" pass single_edge --top icgf "lg:$W/lg_good" --emit-dir "lg:$W/lg_norm" \
       --workdir "$W/pw" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "pass single_edge failed on a canonical ICG"; }
grep -q "gated clock(s) folded into an enable" <<<"$out" \
  || { echo "$out" | tail -5; fail "the ICG pattern was not recognized (no gate folded into an enable)"; }
grep -q "P=1 slots" <<<"$out" \
  || { echo "$out" | tail -5; fail "the ICG needed more than one slot: a gate has ONE commit edge, so folding it into the enable must leave P=1 -- a phase divider here means the gate was treated as a second commit class"; }
echo "ok: the ICG pattern is recognized and folded into a flop enable at P=1"

emit() { # <lgdir> <outdir>
  rm -rf "$2"
  "$LHD" compile "lg:$1" --top icgf.icgf --emit-dir "verilog:$2" --workdir "$W/vw_$(basename "$2")" \
    >"$W/e_$(basename "$2").log" 2>&1 \
    || { tail -5 "$W/e_$(basename "$2").log"; fail "verilog emission from $1 failed"; }
}
emit "$W/lg_good" "$W/v_src"
emit "$W/lg_norm" "$W/v_norm"
cat "$W"/v_src/*.v > "$W/src.v"
sed 's/^module icgf(/module icgf_n(/' "$W"/v_norm/*.v > "$W/norm.v"

grep -q "always_latch"      "$W/src.v"  || fail "the SOURCE emission lost its enable latch"
grep -q "always_latch"      "$W/norm.v" && fail "the NORMALIZED emission still holds the enable latch"

# The gated clock survives as a DECLARED NET, not an inline expression. cgen
# deliberately stopped emitting `always @(posedge (clk & enl))`: it is legal
# Verilog that LiveHD's OWN reader rejects ("the clock must be a plain signal"),
# so cgen output could not be read back -- see the comment on the clock_pin
# lookup in inou/cgen/cgen_verilog.cpp and tests/equiv/mclk_derived. Match the
# net form, and check what the greps actually meant: the SOURCE flop is clocked
# by a gate of clk, the NORMALIZED one directly by clk.
flop_clk_of() { sed -n 's/^[[:space:]]*always @(posedge \([A-Za-z_][A-Za-z0-9_]*\).*/\1/p' "$1" | head -1; }

src_clk="$(flop_clk_of "$W/src.v")"
[ -n "$src_clk" ] || fail "the SOURCE emission has no posedge flop at all"
[ "$src_clk" != clk ] || fail "the SOURCE emission lost its gated clock (the flop is clocked by clk directly)"
grep -qE "^[[:space:]]*$src_clk[[:space:]]*=[[:space:]]*clk[[:space:]]*&" "$W/src.v" \
  || { grep -nE "$src_clk" "$W/src.v" | head -3; fail "the SOURCE flop's clock net '$src_clk' is not driven by clk & <enable>"; }

norm_clk="$(flop_clk_of "$W/norm.v")"
[ "$norm_clk" = clk ] || fail "the NORMALIZED emission still holds a gated clock (flop clocked by '$norm_clk')"
echo "ok: the normalized netlist has a plain posedge flop with an enable"

# ---- lhd lec: real verdicts, both directions --------------------------------
"$LHD" lec --impl "lg:$W/lg_good" --ref "lg:$W/lg_good" --top icgf --workdir "$W/l1" >"$W/l1.log" 2>&1
[ $? -eq 0 ] || { tail -5 "$W/l1.log"; fail "an ICG design does not prove against itself (it used to be REFUSED as a second clock domain)"; }
echo "ok: lec PROVES an ICG design against itself"

"$LHD" lec --impl "lg:$W/lg_bad" --ref "lg:$W/lg_good" --top icgf --workdir "$W/l2" >"$W/l2.log" 2>&1
[ $? -eq 0 ] && { tail -5 "$W/l2.log"; fail "a real logic difference inside an ICG design was NOT caught -- the gate is masking it"; }
grep -q "REFUTED" "$W/l2.log" \
  || { tail -5 "$W/l2.log"; fail "the mutated ICG design must be REFUTED verbatim, not merely inconclusive"; }
echo "ok: lec REFUTES a real difference inside an ICG design (not vacuous)"

# ---- iverilog: the rewrite is semantics-preserving --------------------------
if ! command -v iverilog >/dev/null 2>&1 || ! command -v vvp >/dev/null 2>&1; then
  echo "note: iverilog/vvp not found — the INDEPENDENT check of the rewrite was SKIPPED."
  echo "PASS: single_edge_icg_test (lec legs only)"
  exit 0
fi

cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
module tb;
  reg clk = 0, reset = 1, en = 0;
  reg [7:0] d = 0;
  wire [7:0] qs, qn;
  integer k, errs = 0;
  always #10 clk = ~clk;                 // rise at 10+20k, fall at 20+20k
  icgf   s(.clk(clk), .en(en), .d(d), .q(qs), .reset(reset));
  icgf_n n(.clk(clk), .en(en), .d(d), .q(qn), .reset(reset));
  initial begin
    reset = 1; en = 0; d = 0;
    #25 reset = 0;                       // after period 0's rise AND fall
    for (k = 1; k < 30; k = k + 1) begin
      // Stimulus changes mid-LOW phase, well clear of the rise -- so `en` is
      // STABLE across the sampling edge, which is the ICG's own contract (the
      // enable latch exists precisely to guarantee that). Toggling `en` AT the
      // edge is a design error, not something the rewrite must reproduce.
      #(5 + 20*k - $time);
      en = (k % 3) != 0;                 // gate on/off in a 3-cycle pattern
      d  = (11 + 17*k) & 8'hff;
      #(18 + 20*k - $time);              // sample mid-HIGH, after the rise
      if (qs !== qn) begin
        errs = errs + 1;
        if (errs < 6) $display("MISMATCH k=%0d @%0t src=%0d norm=%0d en=%0b", k, $time, qs, qn, en);
      end
    end
    if (errs == 0) $display("ICG_DIFF_OK"); else $display("ICG_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF

iverilog -g2012 -o "$W/tb.vvp" "$W/src.v" "$W/norm.v" "$W/tb.v" >"$W/iv.log" 2>&1 \
  || { cat "$W/iv.log"; fail "iverilog could not elaborate the source+normalized pair"; }
out="$(vvp "$W/tb.vvp" 2>&1)"
grep -q "ICG_DIFF_OK" <<<"$out" \
  || { echo "$out" | head -8; fail "the normalized design does not match the real always_latch + gated-clock source under iverilog"; }
echo "ok: source ICG and normalized flop-with-enable agree over 29 cycles under iverilog"

# NEGATIVE CONTROL: use the enable latch's HELD Q instead of the value it passes
# through. That is the L1 error the bypass exists to prevent, and it must fail
# here -- otherwise the agreement above proves nothing about the bypass.
sed 's/^if (en) begin/if (enl) begin/' "$W/norm.v" > "$W/norm_late.v"
if ! cmp -s "$W/norm.v" "$W/norm_late.v"; then
  iverilog -g2012 -o "$W/late.vvp" "$W/src.v" "$W/norm_late.v" "$W/tb.v" >"$W/iv2.log" 2>&1 \
    || { cat "$W/iv2.log"; fail "could not build the L1 negative control"; }
  out="$(vvp "$W/late.vvp" 2>&1)"
  grep -q "ICG_DIFF_FAIL" <<<"$out" \
    || fail "reading the enable latch's HELD Q still matched the source -- the harness cannot see a one-cycle-late enable, so the agreement above is not evidence"
  echo "ok: reading the enable latch's held Q instead FAILS (the bypass is load-bearing)"
else
  fail "the L1 negative control did not apply (the normalized netlist does not spell its enable as expected)"
fi

# ---- the shape REAL designs actually have: an INSTANTIATED ICG cell ---------
# Nobody spells an ICG inline. They instantiate a cell, so the gate structure
# sits ONE MODULE LEVEL AWAY and the flop's clock_pin is an opaque `Sub` output
# that nothing downstream can recognize as a gate. This is minion's shape (32
# `prim_clk_gate` instances) and it is the form that matters in practice.
#
# The fix is to inline exactly those cells -- an instance that drives a state
# element's clock_pin AND whose def contains a latch -- so the gate lands in the
# parent body and the fold above applies. Inlining is unconditionally
# semantics-preserving, so it is safe to do before any analysis.
cat > "$W/cell.v" <<'EOF'
module prim_clk_gate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule

module dut(input clk, input en, input [7:0] d, output [7:0] q);
  logic gclk;
  prim_clk_gate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  reg [7:0] f;
  always @(posedge gclk) f <= d;
  assign q = f;
endmodule
EOF
sed 's/f <= d;/f <= d + 8'"'"'d1;/' "$W/cell.v" > "$W/cell_bad.v"
cellbuild() { # <name> <src>
  rm -rf "$W/lgc_$1"
  "$LHD" compile "$2" --reader slang --top dut --emit-dir "lg:$W/lgc_$1" --workdir "$W/cwc_$1" \
    >"$W/cc_$1.log" 2>&1 || { tail -5 "$W/cc_$1.log"; fail "compile of $2 failed"; }
}
cellbuild good "$W/cell.v"
cellbuild bad  "$W/cell_bad.v"

rm -rf "$W/lgc_norm"
out="$("$LHD" pass single_edge --top dut "lg:$W/lgc_good" --emit-dir "lg:$W/lgc_norm" --workdir "$W/pwc" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "pass single_edge failed on an INSTANTIATED ICG cell"; }
grep -q "inlined 1 clock-gate cell" <<<"$out" \
  || { echo "$out" | tail -5; fail "the instantiated clock-gate cell was not inlined, so its gate stayed invisible one module level away"; }
grep -q "gated clock(s) folded into an enable" <<<"$out" \
  || { echo "$out" | tail -5; fail "the inlined cell's gate was not folded into the flop enable"; }
echo "ok: an INSTANTIATED clock-gate cell is inlined and folded into the flop enable"

"$LHD" lec --impl "lg:$W/lgc_good" --ref "lg:$W/lgc_good" --top dut --workdir "$W/lc1" >"$W/lc1.log" 2>&1
[ $? -eq 0 ] || { tail -5 "$W/lc1.log"; fail "a design with an instantiated ICG cell does not prove against itself"; }
"$LHD" lec --impl "lg:$W/lgc_bad" --ref "lg:$W/lgc_good" --top dut --workdir "$W/lc2" >"$W/lc2.log" 2>&1
[ $? -eq 0 ] && { tail -5 "$W/lc2.log"; fail "a real difference behind an instantiated ICG cell was NOT caught"; }
grep -q "REFUTED" "$W/lc2.log" || { tail -5 "$W/lc2.log"; fail "the mutated instantiated-ICG design must be REFUTED verbatim"; }
echo "ok: lec PROVES and REFUTES correctly through an instantiated clock-gate cell"

# ---- a latch gated by CLOCK **AND** DATA -------------------------------------
# minion's `prim_phase_pair` shape: `always_latch if (!clk && lo_en) q <= d`.
# The window is the clock's, so the commit class is the clock's edge and the data
# operand is an ordinary enable -- but the whole cone resolves to the `And`, so
# without recognizing it the latch classifies as DATA-gated and the lowering
# leaves the CLOCK BEING READ AS DATA in the enable. Worse, with no Clock-role
# element left in the design the retyped flop gets no clock at all and cgen emits
# `always @(posedge 'hx)`.
#
# Note the descent this needs: a flop's gate is usually the `And` itself, but a
# LATCH's enable arrives WRAPPED (tolg lowers the condition to
# `enable = Mux(cone, 0, 1)`), so the resolver has to walk the boolean-shaping
# wrappers before it can see the gate -- and fold any inversion it passes.
cat > "$W/cd.v" <<'EOF'
module dut(input clk, input en, input [7:0] d, output [7:0] q);
  logic [7:0] l;
  always_latch if (!clk && en) l <= d;   // transparent-LOW, ALSO gated by data
  assign q = l;
endmodule
EOF
rm -rf "$W/lgcd" "$W/lgcd_n"
"$LHD" compile "$W/cd.v" --reader slang --top dut --emit-dir "lg:$W/lgcd" --workdir "$W/cwcd" \
  >"$W/ccd.log" 2>&1 || { tail -5 "$W/ccd.log"; fail "compile of the clk-and-data latch failed"; }
out="$("$LHD" pass single_edge --top dut "lg:$W/lgcd" --emit-dir "lg:$W/lgcd_n" --workdir "$W/pwcd" 2>&1)"
[ $? -eq 0 ] || { echo "$out" | tail -5; fail "pass single_edge failed on a clk-and-data gated latch"; }
grep -q "gated clock(s) folded into an enable" <<<"$out" \
  || { echo "$out" | tail -5; fail "the clock half of a `clk && data` latch gate was not recognized, so the clock stays in the enable as data"; }

rm -rf "$W/v_cds" "$W/v_cdn"
"$LHD" compile "lg:$W/lgcd"   --top dut --emit-dir "verilog:$W/v_cds" --workdir "$W/vwcds" >/dev/null 2>&1
"$LHD" compile "lg:$W/lgcd_n" --top dut --emit-dir "verilog:$W/v_cdn" --workdir "$W/vwcdn" >/dev/null 2>&1
grep -qE "posedge 'hx" "$W"/v_cdn/*.v \
  && fail "the retyped flop has NO CLOCK (always @(posedge 'hx)) -- a clock-role LATCH must be eligible as the reference clock, or a latch-only design has none"
grep -qE "always @\(posedge clk" "$W"/v_cdn/*.v \
  || { grep -hE "always" "$W"/v_cdn/*.v | head -3; fail "the normalized latch is not a plain posedge flop on clk"; }
echo "ok: a clk-AND-data gated latch lowers to a posedge flop with the data enable"

if command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1; then
  cat "$W"/v_cds/*.v > "$W/cd_src.v"
  sed 's/^module dut(/module dut_n(/' "$W"/v_cdn/*.v > "$W/cd_norm.v"
  cat > "$W/cdtb.v" <<'EOF'
`timescale 1ns/1ps
module tb;
  reg clk=0, en=0; reg [7:0] d=0; wire [7:0] qs,qn; integer k, errs=0;
  always #10 clk = ~clk;                       // P=1: one commit edge, same rate
  dut   s(.clk(clk), .en(en), .d(d), .q(qs));
  dut_n n(.clk(clk), .en(en), .d(d), .q(qn));
  initial begin
    #26;
    for (k=2; k<25; k=k+1) begin
      #(6 + 20*k - $time);  en = (k%3)!=0; d = (11+17*k) & 8'hff;
      // transparent-LOW latch: CLOSED while clk is HIGH, so sample there.
      #(18 + 20*k - $time);
      if (qs !== qn) begin errs=errs+1;
        if (errs<6) $display("MISMATCH k=%0d @%0t src=%0d norm=%0d en=%0b", k,$time,qs,qn,en); end
    end
    if (errs==0) $display("CD_DIFF_OK"); else $display("CD_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF
  iverilog -g2012 -o "$W/cd.vvp" "$W/cd_src.v" "$W/cd_norm.v" "$W/cdtb.v" >"$W/ivcd.log" 2>&1 \
    || { cat "$W/ivcd.log"; fail "iverilog could not elaborate the clk-and-data pair"; }
  out="$(vvp "$W/cd.vvp" 2>&1)"
  grep -q "CD_DIFF_OK" <<<"$out" \
    || { echo "$out" | head -6; fail "the normalized clk-and-data latch does not match the real gated latch under iverilog"; }
  echo "ok: clk-AND-data latch agrees with the real gated latch over 23 cycles under iverilog"
fi

echo "PASS: single_edge_icg_test"
