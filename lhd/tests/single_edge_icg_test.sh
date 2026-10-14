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
# The iverilog legs are the independent oracle and run only with
# LHD_EXTERNAL_SIM=1 (see AGENTS.md); the default run keeps the lec legs.
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

# Optional independent event-simulator legs: skipped by default, and a requested
# leg (LHD_EXTERNAL_SIM=1) with a missing tool FAILS instead of skipping.
external_sim() {
  if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
    echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
    return 1
  fi
  command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
    || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"
}

# The design is VERILOG (slang reader): the hand-built enable latch reads the
# clock level as data, which Pyrope's `Clock` type forbids (qa.md Appendix 6;
# Pyrope's ICG is the Clock_cell `Clock(clock_pin=clk, enable=en)`).
cat > "$W/icg.sv" <<'EOF'
module icgf(input clk, input reset, input en, input [7:0] d, output [7:0] q);
  reg enl;                              // the enable latch, transparent on the LOW phase
  always_latch if (!clk) enl = en;
  wire gclk = clk & enl;                // the gated clock
  reg [7:0] f;
  always @(posedge gclk) if (reset) f <= 0; else f <= d;
  assign q = f;
endmodule
EOF
# A REAL difference inside the same ICG structure: the gate must not mask it.
sed "s/else f <= d;/else f <= d + 8'd1;/" "$W/icg.sv" > "$W/icg_bad.sv"
cmp -s "$W/icg.sv" "$W/icg_bad.sv" && fail "the icg_bad mutation did not apply"

build() { # <name> <src>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$2" --reader slang --top icgf --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" >"$W/c_$1.log" 2>&1 \
    || { tail -5 "$W/c_$1.log"; fail "compile of $2 failed"; }
}
build good "$W/icg.sv"
build bad  "$W/icg_bad.sv"

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
# The gated clock may be emitted inline (`posedge (clk & enl)`) or, since cgen
# force-declares every clock_pin driver (so its own reader can read the output
# back), as a NAMED net: `and_40 = clk & enl;` + `always @(posedge and_40)`.
if ! grep -qE "posedge \(clk" "$W/src.v"; then
  src_gclk="$(sed -n 's/^always @(posedge  *\([A-Za-z_][A-Za-z0-9_.]*\).*$/\1/p' "$W/src.v" | head -1)"
  [ -n "$src_gclk" ] && [ "$src_gclk" != "clk" ] \
    && grep -qE "(^| )${src_gclk} = \(?clk &" "$W/src.v" \
    || fail "the SOURCE emission lost its gated clock"
fi
grep -q "always_latch"      "$W/norm.v" && fail "the NORMALIZED emission still holds the enable latch"
# A regressed fold would keep a DERIVED clock (spelled `posedge <net>` since the
# named-net emission, never `posedge (clk &`): the only posedge allowed is clk.
grep -E "always @\(posedge" "$W/norm.v" | grep -vE "posedge clk[ )]" | grep -q . \
  && fail "the NORMALIZED emission still holds a gated (or derived) clock"
echo "ok: the normalized netlist has a plain posedge flop with an enable"

# ---- lhd lec: real verdicts, both directions --------------------------------
"$LHD" lec --set formal.effort=proof --impl "lg:$W/lg_good" --ref "lg:$W/lg_good" --top icgf --workdir "$W/l1" >"$W/l1.log" 2>&1
[ $? -eq 0 ] || { tail -5 "$W/l1.log"; fail "an ICG design does not prove against itself (it used to be REFUSED as a second clock domain)"; }
grep -qE "^lec: .* PROVEN-(full|bounded)" "$W/l1.log" \
  || { tail -5 "$W/l1.log"; fail "an ICG design exited 0 against itself without a PROVEN verdict"; }
echo "ok: lec PROVES an ICG design against itself"

"$LHD" lec --set formal.effort=proof --impl "lg:$W/lg_bad" --ref "lg:$W/lg_good" --top icgf --workdir "$W/l2" >"$W/l2.log" 2>&1
rc=$?
[ $rc -eq 0 ] && { tail -5 "$W/l2.log"; fail "a real logic difference inside an ICG design was NOT caught -- the gate is masking it"; }
[ $rc -eq 10 ] || { tail -5 "$W/l2.log"; fail "the mutated ICG design must exit with the counterexample class 10 (rc=$rc)"; }
grep -q "^lec: .* REFUTED-cex" "$W/l2.log" \
  || { tail -5 "$W/l2.log"; fail "the mutated ICG design must be REFUTED verbatim, not merely inconclusive"; }
echo "ok: lec REFUTES a real difference inside an ICG design (not vacuous)"

# ---- iverilog: the rewrite is semantics-preserving --------------------------
if external_sim; then
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

  # NEGATIVE CONTROL: use the enable latch's HELD Q instead of the value it
  # passes through. That is the L1 error the bypass exists to prevent, and it
  # must fail here -- otherwise the agreement above proves nothing about it.
  #
  # DERIVE the arm net; do NOT hardcode its spelling. The previous version ran
  # `sed 's/^if (en) begin/if (enl) begin/'`, which never matched any emission
  # and so silently tested nothing (the retained
  # generated/master_port/d2_runtime/single_edge_icg_test_extsim.log records it
  # as "did not apply", with the positive leg passing beside it). cgen does not
  # spell the enable that way: the direct-slang shape emits
  # `if ((en & ((reset) == ('sb0))))` and the yosys shape emits
  # `if (sext_116)`. What IS stable in both is that the held latch survives as
  # `reg enl` fed by `___next_enl = <arm>;`, so the arm is read off that line
  # and every occurrence of it is swapped for the held `enl`.
  l1_mutate() { # <norm.v> <out.v>  -> prints the arm net, or fails
    local arm tgt cond
    arm="$(sed -n 's/^[[:space:]]*___next_enl[[:space:]]*=[[:space:]]*\([A-Za-z_][A-Za-z0-9_]*\)[[:space:]]*;.*$/\1/p' "$1" | head -1)"
    [ -n "$arm" ] || return 1
    # The target is the GATED FLOP's enable only: the `if (...)` guarding
    # `f <= ___next_f;`. Substituting the arm everywhere would also rewrite its
    # own declaration and definition (the yosys shape defines the arm as
    # `sext_116 = $signed(mux_56[0:0]);`), which would collide with the real
    # `enl` and build a different design rather than the L1 defect.
    tgt="$(grep -n 'f <= ___next_f;' "$1" | head -1 | cut -d: -f1)"
    [ -n "$tgt" ] || return 1
    cond="$(awk -v t="$tgt" 'NR < t && /if[ \t]*\(/ { l = NR } END { print l }' "$1")"
    [ -n "$cond" ] || return 1
    sed "${cond}s/\\b${arm}\\b/enl/g" "$1" > "$2"
    printf '%s' "$arm"
  }
  arm_net="$(l1_mutate "$W/norm.v" "$W/norm_late.v")" \
    || fail "the L1 negative could not find the enable latch's arm (`___next_enl = <net>;`) in the normalized netlist"
  cmp -s "$W/norm.v" "$W/norm_late.v" \
    && fail "the L1 negative control did not apply: arm net '$arm_net' was found but substituting it changed nothing"
  echo "ok: the L1 mutation applied (arm net '$arm_net' -> held 'enl')"
  iverilog -g2012 -o "$W/late.vvp" "$W/src.v" "$W/norm_late.v" "$W/tb.v" >"$W/iv2.log" 2>&1 \
    || { cat "$W/iv2.log"; fail "could not build the L1 negative control (it must EXECUTE, not merely be generated)"; }
  out="$(vvp "$W/late.vvp" 2>&1)"
  grep -qE "ICG_DIFF_OK|ICG_DIFF_FAIL" <<<"$out" \
    || { echo "$out" | head -8; fail "the L1 negative control did not run to a verdict"; }
  grep -q "ICG_DIFF_FAIL" <<<"$out" \
    || fail "reading the enable latch's HELD Q still matched the source -- the harness cannot see a one-cycle-late enable, so the agreement above is not evidence"
  echo "ok: reading the enable latch's held Q instead FAILS (the bypass is load-bearing)"
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

"$LHD" lec --set formal.effort=proof --impl "lg:$W/lgc_good" --ref "lg:$W/lgc_good" --top dut --workdir "$W/lc1" >"$W/lc1.log" 2>&1
[ $? -eq 0 ] || { tail -5 "$W/lc1.log"; fail "a design with an instantiated ICG cell does not prove against itself"; }
grep -qE "^lec: .* PROVEN-(full|bounded)" "$W/lc1.log" \
  || { tail -5 "$W/lc1.log"; fail "a design with an instantiated ICG cell exited 0 without a PROVEN verdict"; }
"$LHD" lec --set formal.effort=proof --impl "lg:$W/lgc_bad" --ref "lg:$W/lgc_good" --top dut --workdir "$W/lc2" >"$W/lc2.log" 2>&1
rc=$?
[ $rc -eq 0 ] && { tail -5 "$W/lc2.log"; fail "a real difference behind an instantiated ICG cell was NOT caught"; }
[ $rc -eq 10 ] || { tail -5 "$W/lc2.log"; fail "the mutated instantiated-ICG design must exit with the counterexample class 10 (rc=$rc)"; }
grep -q "^lec: .* REFUTED-cex" "$W/lc2.log" || { tail -5 "$W/lc2.log"; fail "the mutated instantiated-ICG design must be REFUTED verbatim"; }
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

if external_sim; then
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


# ---- the CORE-ET prim_clk_gate shape, and its same-phase twin ---------------
#
# The fixture above writes the enable latch as `always_latch if (!clk) enl = en;`
# -- a BLOCKING assignment in always_latch. CORE-ET's
# hw/ip/tech_generic/prim_clk_gate/rtl/prim_clk_gate.sv writes the SAME latch in
# the original cell's event-control form, with a NON-BLOCKING assignment and a
# scan-mode term:
#
#     always @(clk_i or en_i or dft_i.scanmode)
#       if (!clk_i) en_latch <= en_i | dft_i.scanmode;
#     assign clk_o = clk_i & en_latch;
#
# Same contract, different spelling -- and the reader lowers this one's window as
# `Get_mask(Mux(Ror(clk), c0, c1), [0,1))`. `values_with_root` understood And/Or/
# Clock_cell and nothing else, so the window came back kAny and the
# opposite-phase test failed BOTH halves: a textbook ICG was refused with
# `clock-gate-latch-phase` on minion_dcache_top (`flop_75936:...count_q`
# through `latch_50396:...u_clk_gate.en_latch`).
#
# The three pass-throughs added to values_with_root (1-bit Ror, identity
# Get_mask, Mux with a decided selector) are exact, so a genuinely same-phase
# latch must STILL be refused. That is the second design here, and it is what
# makes this a discriminating control rather than a smoke test: if the fix ever
# degrades into "unknown means fine", the same-phase leg passes and fails loudly.
cat > "$W/icg_ec.sv" <<'EOF'
module icgf(input clk, input reset, input en, input scan, input [7:0] d, output [7:0] q);
  reg enl;
  always @(clk or en or scan) if (!clk) enl <= en | scan;
  wire gclk = clk & enl;
  reg [7:0] f;
  always @(posedge gclk) if (reset) f <= 0; else f <= d;
  assign q = f;
endmodule
EOF
# The ONLY change is the transparent phase: open while the gated clock is HIGH.
sed 's/if (!clk) enl <= en | scan;/if (clk) enl <= en | scan;/' "$W/icg_ec.sv" > "$W/icg_ec_same.sv"
cmp -s "$W/icg_ec.sv" "$W/icg_ec_same.sv" && fail "the same-phase mutation did not apply"

# The YOSYS reader, deliberately: B01 is compiled with `--reader yosys-slang`,
# and it is that reader which lowers this latch's window as
# `Get_mask(Mux(Ror(clk), c0, c1), [0,1))`. The direct slang reader produces a
# different, already-readable shape, so building these two designs with it would
# make the control vacuous -- measured: with `--reader slang` both legs pass even
# with the pass-throughs removed.
build_top() { # <name> <src> <top>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$2" --reader yosys-slang --top "$3" --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" >"$W/c_$1.log" 2>&1 \
    || { tail -5 "$W/c_$1.log"; fail "compile of $2 failed"; }
}
build_top ec      "$W/icg_ec.sv"      icgf
build_top ec_same "$W/icg_ec_same.sv" icgf

# POSITIVE: the event-control / non-blocking / scan-OR spelling is recognized.
rm -rf "$W/lg_ec_norm"
out="$("$LHD" pass single_edge --top icgf "lg:$W/lg_ec" --emit-dir "lg:$W/lg_ec_norm" \
       --workdir "$W/pw_ec" 2>&1)"
rc=$?
[ $rc -eq 0 ] \
  || { echo "$out" | tail -6; fail "the CORE-ET prim_clk_gate spelling was refused (rc=$rc); an opposite-phase ICG must be recognized whatever its RTL form"; }
grep -q "gated clock(s) folded into an enable" <<<"$out" \
  || { echo "$out" | tail -6; fail "the event-control ICG was not folded into an enable"; }
grep -q "P=1 slots" <<<"$out" \
  || { echo "$out" | tail -6; fail "the event-control ICG did not stay at P=1"; }
echo "ok: the CORE-ET prim_clk_gate spelling (event control, non-blocking, scan OR) is recognized"

# ...AND IT SIMULATES. Recognition is not the gate: `single_edge` exiting 0 and
# reporting a folded gate says the classifier read the phase, nothing about the
# design it produced. It produced `q = x` -- the yosys reader spells the latch's
# hold branch as a don't-care constant two identity wrappers down
# (`Sext(Get_mask(Mux(Ror(clk), 1'sb?, en|scan)))`), so the "transparent arm"
# the bypass took was the clock-muxed wrapper, which is X at the sampling edge.
# Nothing caught it because the iverilog legs above run the DIRECT-SLANG shape.
# This leg runs the ORIGINAL RTL against the normalized output, P=1 so both sit
# on the same clock, with `en` and `scan` moving in the low phase and `en` also
# moving MID-HIGH where the real latch is shut.
if external_sim; then
  emit "$W/lg_ec"      "$W/v_ec_src"
  emit "$W/lg_ec_norm" "$W/v_ec_norm"
  sed 's/^module icgf(/module icgf_n(/' "$W"/v_ec_norm/*.v > "$W/ec_norm.v"
  cat > "$W/ec_tb.v" <<'EOF'
`timescale 1ns/1ps
module tb;
  reg clk = 0, reset = 1, en = 0, scan = 0;
  reg [7:0] d = 0;
  wire [7:0] qs, qn;
  integer errs = 0, checks = 0, k = 0;
  always #10 clk = ~clk;                 // rise 10+20p, fall 20+20p
  icgf   s(.clk(clk), .reset(reset), .en(en), .scan(scan), .d(d), .q(qs));
  icgf_n n(.clk(clk), .reset(reset), .en(en), .scan(scan), .d(d), .q(qn));
  // LOW-phase stimulus: the ICG's own contract, stable across the rise.
  always @(negedge clk) begin
    #6 d    <= (k * 8'd37 + 8'd11) & 8'hff;
       en   <= (k % 3) != 0;
       scan <= (k % 7) == 0;
       k    <= k + 1;
  end
  // HIGH-phase disturbance: the latch is SHUT, so neither side may react.
  always @(posedge clk) #6 en <= ~en;
  always @(posedge clk) begin
    #16 if (!reset) begin
          checks <= checks + 1;
          if (qs !== qn) begin
            errs = errs + 1;
            if (errs < 8) $display("MISMATCH @%0t source=%0d normalized=%0d", $time, qs, qn);
          end
        end
  end
  initial begin
    #27 reset = 0;
    #600;
    if (checks < 20)    $display("ICG_EC_VACUOUS checks=%0d", checks);
    else if (errs != 0) $display("ICG_EC_FAIL errs=%0d", errs);
    else                $display("ICG_EC_OK checks=%0d", checks);
    $finish;
  end
endmodule
EOF
  iverilog -g2012 -o "$W/ec.vvp" "$W/icg_ec.sv" "$W/ec_norm.v" "$W/ec_tb.v" >"$W/iv_ec.log" 2>&1 \
    || { tail -5 "$W/iv_ec.log"; fail "iverilog could not elaborate the CORE-ET-spelling pair"; }
  out="$(timeout 120 vvp "$W/ec.vvp" 2>&1)"
  grep -qE "ICG_EC_OK|ICG_EC_FAIL|ICG_EC_VACUOUS" <<<"$out" \
    || { echo "$out" | head -8; fail "the CORE-ET-spelling differential did not run to a verdict"; }
  grep -q "ICG_EC_VACUOUS" <<<"$out" && { echo "$out" | head -4; fail "the CORE-ET-spelling differential observed too few cycles to mean anything"; }
  grep -q "ICG_EC_OK" <<<"$out" \
    || { echo "$out" | head -8; fail "the CORE-ET prim_clk_gate spelling normalizes to a design that does not match its own RTL"; }
  echo "ok: the CORE-ET spelling agrees with its ORIGINAL RTL over 30 cycles under iverilog"
fi

# NEGATIVE, and it must fail for the RIGHT reason: a latch transparent on the
# GATED phase passes an enable change through while the gated clock is high.
out="$("$LHD" pass single_edge --top icgf "lg:$W/lg_ec_same" --emit-dir "lg:$W/lg_ec_same_norm" \
       --workdir "$W/pw_ec_same" 2>&1)"
rc=$?
[ $rc -eq 0 ] \
  && { echo "$out" | tail -6; fail "a SAME-phase enable latch was accepted -- the opposite-phase guard has been defeated, not taught"; }
grep -q "clock-gate-latch-phase" <<<"$out" \
  || { echo "$out" | tail -6; fail "the same-phase latch was refused, but not by the phase guard (wrong refusal: the control proves nothing)"; }
echo "ok: the same-phase twin is still refused by clock-gate-latch-phase"


# ---- ONE GATE, a RISING and a FALLING consumer -----------------------------
#
# This is CORE-ET minion_dcache_top's shape: `latch_50396:...u_clk_gate.en_latch`
# feeds `flop_75936:...count_q` (posedge) AND
# `flop_76000:...u_tmp_data.wr_data_del_q` (negedge). The negedge half is real
# RTL -- B01's filelist selects tech_generic/prim_rf_1r1w{,_reg,_par}_preview,
# each `always_ff @(negedge rf_clk_i)` for the data delay against
# `always_ff @(posedge rf_clk_i)` for the array: a half-cycle register file.
#
# The arm bypass is NOT exact for the falling consumer -- the latch OPENS at
# that edge, so its Q is the value held across the high phase while its D is the
# live enable. So the GATE GUARD (and only the gate guard) is sampled in the
# rising slot and held; the consumer's OWN enable and the slot predicate combine
# at its commit. Sampling the conjunction instead would freeze the local enable
# half a period early, which is why `len` below moves in BOTH directions during
# the high phase.
cat > "$W/icg_dual.sv" <<'EOF'
module icgf(input clk, input reset, input en, input scan, input len, input [7:0] d,
            output [7:0] qr, output [7:0] qf);
  reg enl;
  always @(clk or en or scan) if (!clk) enl <= en | scan;
  wire gclk = clk & enl;
  reg [7:0] fr, ff;
  always @(posedge gclk) if (reset) fr <= 0; else fr <= d;   // rising consumer
  always @(negedge gclk) if (len) ff <= d;                   // falling + LOCAL enable
  assign qr = fr;
  assign qf = ff;
endmodule
EOF
build_top dual "$W/icg_dual.sv" icgf
out="$("$LHD" pass single_edge --top icgf "lg:$W/lg_dual" --emit-dir "lg:$W/lg_dual_norm" \
       --workdir "$W/pw_dual" 2>&1)"
rc=$?
[ $rc -eq 0 ] \
  || { echo "$out" | tail -6; fail "one gate driving a rising AND a falling consumer was refused (rc=$rc)"; }
grep -q "sampled-held" <<<"$out" \
  || { echo "$out" | tail -6; fail "the falling consumer normalized WITHOUT a sampled gate guard; its enable would be the live arm"; }
grep -q "P=2 slots" <<<"$out" \
  || { echo "$out" | tail -6; fail "a rising and a falling consumer need two slots"; }
echo "ok: one gate drives both edges, the falling consumer on a sampled-held guard at P=2"

if external_sim; then
  emit "$W/lg_dual"      "$W/v_dual_src"
  emit "$W/lg_dual_norm" "$W/v_dual_norm"
  sed 's/^module icgf(/module icgf_n(/' "$W"/v_dual_norm/*.v > "$W/dual_norm.v"
  cat > "$W/dual_tb.v" <<'EOF'
`timescale 1ns/1ps
// The normalized oscillator starts 5 ns LATE so its posedges land at
// 10,20,30,... -- slot 0 on the source's rise (10+20p), slot 1 on its fall
// (20+20p). With `always #5` from 0 they would be at 5,15,25,... i.e. a HALF
// PHASE AHEAD of the source, and the two designs would sample different inputs.
module tb;
  reg clk_s = 0, clk_n = 0, reset = 1, en = 0, scan = 0, len = 1;
  reg [7:0] d = 0;
  wire [7:0] sr, sf, nr, nf;
  integer errs = 0, checks = 0, hi = 0, lo_up = 0, lo_dn = 0, k = 0;
  reg     prologue_bad = 0;
  always #10 clk_s = ~clk_s;
  initial begin clk_n = 0; #5; forever #5 clk_n = ~clk_n; end
  icgf   s(.clk(clk_s), .reset(reset), .en(en), .scan(scan), .len(len), .d(d), .qr(sr), .qf(sf));
  icgf_n n(.clk(clk_n), .reset(reset), .en(en), .scan(scan), .len(len), .d(d), .qr(nr), .qf(nf));
  always @(negedge clk_s) begin          // LOW phase: the ICG's own contract
    #6 d    <= (k * 8'd37 + 8'd11) & 8'hff;
       en   <= (k % 3) != 0;
       scan <= (k % 11) == 0;
       len  <= (k % 2) == 0;
       k    <= k + 1;
  end
  always @(posedge clk_s) begin          // HIGH phase: latch SHUT
    #6 en  <= ~en;                       //   gate enable must be ignored
       len <= ~len;                      //   LOCAL enable must NOT be
       hi  <= hi + 1;
       if (len) lo_dn <= lo_dn + 1; else lo_up <= lo_up + 1;
  end
  // THE STARTUP BOUNDARY, asserted rather than chosen. `prim_clk_gate`'s latch
  // is `always @(clk or en or scan) if (!clk) enl <= ...`; with clk starting LOW
  // there is no EVENT until clk rises, so `enl` is x until the FIRST LOW PHASE
  // AFTER that. During that window the source's `gclk = clk & enl` goes 0 -> x,
  // which Verilog treats as a POSEDGE, so the source commits under an UNDEFINED
  // gate while the normalized design (which has no gated clock) correctly does
  // not. Comparing there compares undefined behaviour, not the rewrite.
  //
  // So comparison starts when the SOURCE's gate is established, and the run
  // FAILS if it never is -- a boundary that cannot silently swallow the test.
  // Retained evidence for the excluded observation:
  // generated/master_port/b01_extsim_20261014/startup_evidence/.
  //
  // So the prologue is DRIVEN, not waited out: reset is held past the first
  // rise that happens with a DEFINED gate, which is the rising consumer's own
  // reset contract (`if (reset) fr <= 0`), so both designs enter the comparison
  // from the same state. The falling consumer has no reset and is x on both
  // sides until its first write, which compares equal.
  reg gate_ready = 0;
  always @(s.enl) if (s.enl !== 1'bx) gate_ready <= 1;
  always @(negedge clk_s) begin
    #8 if (!reset && gate_ready) begin
         checks <= checks + 1;
         // The prologue must have WORKED: a reset commit under a defined gate
         // leaves the rising output defined on both sides. If it is still x the
         // comparison below would be vacuous or wrong, so say so.
         if (checks == 1 && (sr === 8'bx || nr === 8'bx)) prologue_bad <= 1;
         if (sr !== nr) begin errs = errs + 1;
           if (errs < 8) $display("MISMATCH qr @%0t source=%0d normalized=%0d", $time, sr, nr); end
         if (sf !== nf) begin errs = errs + 1;
           if (errs < 8) $display("MISMATCH qf @%0t source=%0d normalized=%0d", $time, sf, nf); end
       end
  end
  initial begin
    #51 reset = 0;   // past the rise at t=30, which happens with enl defined
    #600;
    if (!gate_ready)               $display("DUAL_GATE_NEVER_READY");
    else if (prologue_bad)         $display("DUAL_PROLOGUE_BAD");
    else if (checks < 20)          $display("DUAL_VACUOUS checks=%0d", checks);
    else if (hi < 20)              $display("DUAL_NO_HIGH_MOVES n=%0d", hi);
    else if (lo_up < 5 || lo_dn < 5) $display("DUAL_LOCAL_ONESIDED up=%0d dn=%0d", lo_up, lo_dn);
    else if (errs != 0)            $display("DUAL_FAIL errs=%0d", errs);
    else                           $display("DUAL_OK checks=%0d high=%0d up=%0d dn=%0d", checks, hi, lo_up, lo_dn);
    $finish;
  end
endmodule
EOF
  iverilog -g2012 -o "$W/dual.vvp" "$W/icg_dual.sv" "$W/dual_norm.v" "$W/dual_tb.v" >"$W/iv_dual.log" 2>&1 \
    || { tail -5 "$W/iv_dual.log"; fail "iverilog could not elaborate the dual-edge pair"; }
  out="$(timeout 120 vvp "$W/dual.vvp" 2>&1)"
  grep -qE "DUAL_OK|DUAL_FAIL|DUAL_VACUOUS|DUAL_NO_HIGH_MOVES|DUAL_LOCAL_ONESIDED|DUAL_GATE_NEVER_READY|DUAL_PROLOGUE_BAD" <<<"$out" \
    || { echo "$out" | head -8; fail "the dual-edge differential did not run to a verdict"; }
  grep -qE "DUAL_VACUOUS|DUAL_NO_HIGH_MOVES|DUAL_LOCAL_ONESIDED|DUAL_GATE_NEVER_READY|DUAL_PROLOGUE_BAD" <<<"$out" \
    && { echo "$out" | head -4; fail "the dual-edge differential did not exercise what it claims"; }
  grep -q "DUAL_OK" <<<"$out" \
    || { echo "$out" | head -8; fail "the sampled-held falling consumer does not match its ORIGINAL RTL"; }
  echo "ok: both consumers of one gate agree with the ORIGINAL RTL (gate moves high-phase, local enable both ways)"

  # NEGATIVE: the falling consumer on the LIVE arm instead of the held guard --
  # the very error the sampling exists to prevent. Derived from the netlist, not
  # hardcoded, and required to apply, elaborate, run and MISMATCH.
  hold="$(sed -n 's/^if ((\(flop_[0-9]*\) & single_edge_phase)) begin$/\1/p' "$W/dual_norm.v" | head -1)"
  [ -n "$hold" ] || fail "no sampled-held guard found in the normalized netlist"
  live="$(sed -n "s/^  ___next_${hold} = \([A-Za-z_][A-Za-z0-9_]*\);\$/\1/p" "$W/dual_norm.v" | head -1)"
  [ -n "$live" ] || fail "could not find what the held guard samples"
  sed "s/^if ((${hold} & single_edge_phase)) begin\$/if ((${live} \& single_edge_phase)) begin/" \
      "$W/dual_norm.v" > "$W/dual_live.v"
  cmp -s "$W/dual_norm.v" "$W/dual_live.v" \
    && fail "the held-vs-live negative did not apply ($hold -> $live)"
  iverilog -g2012 -o "$W/dual_live.vvp" "$W/icg_dual.sv" "$W/dual_live.v" "$W/dual_tb.v" >"$W/iv_dual_live.log" 2>&1 \
    || { tail -5 "$W/iv_dual_live.log"; fail "could not build the held-vs-live negative"; }
  out="$(timeout 120 vvp "$W/dual_live.vvp" 2>&1)"
  grep -qE "DUAL_OK|DUAL_FAIL" <<<"$out" || { echo "$out" | head -6; fail "the held-vs-live negative did not run to a verdict"; }
  grep -q "DUAL_FAIL" <<<"$out" \
    || fail "reading the LIVE arm at the falling edge still matched the source -- the harness cannot see the sampling, so the agreement above is not evidence"
  echo "ok: reading the live arm instead of the held guard FAILS (the sampling is load-bearing)"
fi

# ---- one guard per GATE, not per consumer ----------------------------------
#
# The sampled guard is keyed on a PLANNED identity -- the gate's clock root, the
# edge it is sampled against, and the guard cone's own pins -- built before any
# node exists, which is what pass/lec/phase_sched.cpp does (root_key + polarity
# + cone_digest, then guard_cones). Keying on the folded AND node instead would
# give a COMPOUND guard a fresh id per consumer and so a separate held state.
#
# LIMIT, stated because the control below does not cover it: the only RTL shape
# that puts MORE THAN ONE pin in a gate's enable list is a gate CHAIN
# (`gate(gate(clk,en0),en1)`), and that shape is currently refused for an
# unrelated reason -- the inner enable latch is clocked by the GATED clock, so
# it is not opposite-phase against the root. So the compound case cannot be
# exercised here, and no control pretends to. What IS checked is that two
# consumers of one gate share a guard and a third on a DIFFERENT gate does not.
cat > "$W/icg_share.sv" <<'EOF'
module icgf(input clk, input reset, input en0, input en1, input en2, input [7:0] d,
            output [7:0] qa, output [7:0] qb, output [7:0] qc, output [7:0] qr);
  reg enlA, enlB;
  always @(clk or en0 or en1) if (!clk) enlA <= en0 & en1;
  always @(clk or en0 or en2) if (!clk) enlB <= en0 & en2;
  wire gclkA = clk & enlA;
  wire gclkB = clk & enlB;
  reg [7:0] fa, fb, fc, fr;
  always @(negedge gclkA) fa <= d;            // two falling consumers, ONE gate
  always @(negedge gclkA) fb <= d + 8'd1;
  always @(negedge gclkB) fc <= d + 8'd2;     // a third, DIFFERENT gate
  always @(posedge gclkA) if (reset) fr <= 0; else fr <= d;
  assign qa = fa; assign qb = fb; assign qc = fc; assign qr = fr;
endmodule
EOF
build_top share "$W/icg_share.sv" icgf
out="$("$LHD" pass single_edge --top icgf "lg:$W/lg_share" --emit-dir "lg:$W/lg_share_norm" \
       --workdir "$W/pw_share" 2>&1)"
rc=$?
[ $rc -eq 0 ] || { echo "$out" | tail -6; fail "two gates with three falling consumers were refused (rc=$rc)"; }
grep -q "3 sampled-held" <<<"$out" \
  || { echo "$out" | tail -4; fail "expected three sampled-held consumers"; }
emit "$W/lg_share_norm" "$W/v_share"
held=$(grep -cE "^reg flop_[0-9]+;" "$W"/v_share/*.v)
[ "$held" = "2" ] \
  || { grep -nE "^reg flop_[0-9]+;|flop_[0-9]+ & single_edge_phase" "$W"/v_share/*.v; \
       fail "three held consumers across TWO gates produced $held held guard(s); expected 2 -- one per gate"; }
shared=$(grep -oE "flop_[0-9]+ & single_edge_phase" "$W"/v_share/*.v | sort | uniq -c | awk '$1==2{n++} END{print n+0}')
[ "$shared" = "1" ] \
  || fail "no held guard is read by two consumers; the gate's guard was minted per consumer"
echo "ok: one sampled guard per GATE (3 held consumers over 2 gates -> 2 guards, one shared by two)"

echo "PASS: single_edge_icg_test"
