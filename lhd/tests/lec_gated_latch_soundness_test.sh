#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd lec` soundness around clock gates, latches and the edge-normalization
# time base. Verdict discipline: UNKNOWN is acceptable, REFUTING an equivalent
# pair is a bug, PROVING a different pair is a worse bug. Every "must not" case
# below reproduced its bug before the fix; the controls pin that the fixes did
# not simply switch the engines off. The independent yosys checker
# (inou/yosys/lgcheck) refutes each "must not be PROVEN" pair; it cannot decide
# the latch pairs of case 1a (inconclusive), which are equivalent by
# construction (one is the hand flattening of the other).
#
#   1. A latch whose window is `!(gated clock)` is TRANSPARENT for the whole
#      period while the gate is off (minion's register-file preview latch).
#      pass.single_edge and the phase schedule both modelled it as "commit at
#      the closing edge iff the gate enable held" -- a HOLD when gated off:
#        a. the latch REFUTED against its own flattening;
#        b. it PROVED equal to a latch that really holds;
#        c. the same model, reached through an OR-ed reset term
#           (`rst | (clk & en_1p)`, minion's write-commit latch), PROVED a twin
#           whose reset only fires under en_1p.
#      graph/latch_contract gated_latch_closed_at now fails such a window closed.
#   2. An ICG whose enable latch is open on the GATED phase (`if (clk) en_l`)
#      passes a change of `en` straight through while the gated clock is high.
#      pass.single_edge bypassed that latch to its arm anyway and PROVED the
#      twin equal to a real clock gate.
#   3. A negedge register compared against a netlist whose DFF cells are
#      ordinary module instances (the Verilog path): the ref needs a P=2 time
#      base, the netlist side was only planned at P=1, and forcing P=2 onto it
#      failed AFTER the ref had been rewritten -- "edge normalization failed
#      after planning", no verdict. The time base now re-plans at the shared P
#      first and flattens that side.

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
T="$PWD/lhd/tests"

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# lec <tag> <impl.v> <ref.v> <top> [--set ...]: sets $V (verdict or "none") and $O (output)
lec() {
  local tag=$1 impl=$2 ref=$3 top=$4
  shift 4
  O="$("$LHD" lec --impl "verilog:$impl" --ref "verilog:$ref" --top "$top" --set formal.simfail_run=false \
    --workdir "$W/w_$tag" --result-json "$W/$tag.json" "$@" 2>&1)"
  V="$(grep -o '"verdict":"[a-z]*"' "$W/$tag.json" 2>/dev/null | head -1 | sed 's/.*:"\(.*\)"/\1/')"
  [ -n "$V" ] || V=none
}
expect() { # <tag> <want: proven|refuted> <impl> <ref> <top> [args]
  local tag=$1 want=$2
  shift 2
  lec "$tag" "$@"
  [ "$V" = "$want" ] || { tail -5 <<<"$O"; fail "$tag: expected $want, got $V"; }
  echo "ok: $tag -> $V"
}
expect_not() { # <tag> <forbidden verdict> <impl> <ref> <top> [args]
  local tag=$1 bad=$2
  shift 2
  lec "$tag" "$@"
  [ "$V" != "$bad" ] || { tail -5 <<<"$O"; fail "$tag: must not be $bad"; }
  echo "ok: $tag -> $V (not $bad)"
}

# ---- 1. transparent-while-gated-off latches ---------------------------------
cat > "$W/dl.v" <<'EOF'
module cg_latch(input logic clk, input logic en, output logic gclk);
  logic en_l;
  always_latch if (!clk) en_l = en;
  assign gclk = clk & en_l;
endmodule
module dl(input clk, input en, input [1:0] d, output [1:0] q);
  wire g0;
  cg_latch u0(.clk(clk), .en(en), .gclk(g0));
  logic [1:0] p;
  always_latch if (!g0) p = d;
  reg [1:0] pq;
  always @(posedge clk) pq <= p;
  assign q = pq;
endmodule
EOF
cat > "$W/dl_flat.v" <<'EOF'
module dl(input clk, input en, input [1:0] d, output [1:0] q);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire g0 = ~(clk & en_l);
  logic [1:0] p;
  always_latch if (g0) p = d;
  reg [1:0] pq;
  always @(posedge clk) pq <= p;
  assign q = pq;
endmodule
EOF
expect_not 1a_flattening refuted "$W/dl_flat.v" "$W/dl.v" dl
grep -q "is not closed for a whole clock phase" <<<"$O" || fail "1a: the refusal must name the latch window"

cat > "$W/dlo.v" <<'EOF'
module dl(input clk, input en, input [1:0] d, output [1:0] q, output [1:0] r);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire g0 = ~(clk & en_l);
  logic [1:0] p;
  always_latch if (g0) p = d;
  reg [1:0] rr;
  always @(negedge clk) rr <= p;
  assign q = p;
  assign r = rr;
endmodule
EOF
sed 's/always_latch if (g0) p = d;/always_latch if (!clk \&\& en_l) p = d;/' "$W/dlo.v" > "$W/dlo_hold.v"
cmp -s "$W/dlo.v" "$W/dlo_hold.v" && fail "1b: twin unchanged"
expect_not 1b_holding_twin proven "$W/dlo_hold.v" "$W/dlo.v" dl

cat > "$W/wc.v" <<'EOF'
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
sed 's/if (rst_i) q_o <= /if (rst_i \&\& en_1p) q_o <= /' "$W/wc.v" > "$W/wc_rg.v"
cmp -s "$W/wc.v" "$W/wc_rg.v" && fail "1c: twin unchanged"
expect_not 1c_or_reset_window proven "$W/wc_rg.v" "$W/wc.v" dut --set formal.lec.semdiff=none

# Controls: a gated latch that really HOLDS while gated off is still decided.
sed 's/always_latch if (g0) p = d;/always_latch if (!clk \&\& en_l) p = d;/' "$W/dl_flat.v" > "$W/dl_hold.v"
sed 's/pq <= p;/pq <= ~p;/' "$W/dl_hold.v" > "$W/dl_hold_m.v"
expect 1d_hold_self proven "$W/dl_hold.v" "$W/dl_hold.v" dl --set formal.lec.semdiff=none
expect 1d_hold_mutant refuted "$W/dl_hold_m.v" "$W/dl_hold.v" dl

# ---- 2. ICG enable latch open on the gated phase -----------------------------
cat > "$W/ph.v" <<'EOF'
module cg(input clk_i, input en_i, output clk_o);
  reg en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module dut(input clk, input en, input [3:0] d, output [3:0] q);
  wire g;
  cg u0(.clk_i(clk), .en_i(en), .clk_o(g));
  reg [3:0] a;
  always @(posedge g) a <= a + d;
  assign q = a;
endmodule
EOF
sed 's/if (!clk_i) en_latch/if (clk_i) en_latch/' "$W/ph.v" > "$W/ph_flip.v"
cmp -s "$W/ph.v" "$W/ph_flip.v" && fail "2a: twin unchanged"
expect_not 2a_phase_flip proven "$W/ph.v" "$W/ph_flip.v" dut
expect_not 2a_phase_flip_rev proven "$W/ph_flip.v" "$W/ph.v" dut
# Controls: a real ICG is still a flop enable, and a data change still refutes.
cat > "$W/ph_en.v" <<'EOF'
module dut(input clk, input en, input [3:0] d, output [3:0] q);
  reg [3:0] a;
  always @(posedge clk) if (en) a <= a + d;
  assign q = a;
endmodule
EOF
sed 's/a <= a + d/a <= a - d/' "$W/ph.v" > "$W/ph_sub.v"
expect 2b_icg_is_enable proven "$W/ph_en.v" "$W/ph.v" dut
expect 2b_data_mutant refuted "$W/ph_sub.v" "$W/ph.v" dut

# ---- 3. a P=2 time base against a netlist of stateful cell instances ---------
# The cell definition sits in BOTH files, as when lgcheck's gensim models are
# appended to each side, so neither side inlines it as absorbed hierarchy.
cat > "$W/nr_ref.v" <<'EOF'
module dffp(input CLK, input D, output reg Q);
  always @(posedge CLK) Q <= D;
endmodule
module dut(input clk, input rst, input [1:0] d, output [1:0] q);
  reg [1:0] f;
  always @(negedge clk) f <= rst ? 2'b00 : (f ^ d);
  assign q = f;
endmodule
EOF
cat > "$W/nr_impl.v" <<'EOF'
module dffp(input CLK, input D, output reg Q);
  always @(posedge CLK) Q <= D;
endmodule
module dut(input clk, input rst, input [1:0] d, output [1:0] q);
  wire nclk = ~clk;
  wire [1:0] f;
  dffp f0(.CLK(nclk), .D(~rst & (f[0] ^ d[0])), .Q(f[0]));
  dffp f1(.CLK(nclk), .D(~rst & (f[1] ^ d[1])), .Q(f[1]));
  assign q = f;
endmodule
EOF
sed 's/(f ^ d)/(f + d)/' "$W/nr_ref.v" > "$W/nr_ref_add.v"
expect 3a_negedge_vs_cells proven "$W/nr_impl.v" "$W/nr_ref.v" dut
grep -q "normalization failed" <<<"$O" && fail "3a: still stops at edge normalization"
expect 3b_negedge_vs_cells_mutant refuted "$W/nr_impl.v" "$W/nr_ref_add.v" dut

# The reported shape: abc_icg_mix mapped onto ICG + DFF cells (a negedge
# register behind a gate is an INV after the ICG cell), compared on the Verilog
# path with the gensim models appended to both sides.
LIB="$T/abc_icg_qn.lib"
"$LHD" synth --reader slang --top abc_icg_mix --workdir "$W/syn" --emit-dir "verilog:$W/netv" \
  --set synth.liberty="$LIB" -- "$T/abc_icg_mix.v" > "$W/synth.log" 2>&1 || { tail -5 "$W/synth.log"; fail "3c: synth"; }
"$LHD" pass liberty gensim "$LIB" --emit "verilog:$W/models.v" --workdir "$W/gm" -q > "$W/gensim.log" 2>&1 \
  || { tail -5 "$W/gensim.log"; fail "3c: gensim"; }
{ cat "$W"/netv/*.v; echo; cat "$W/models.v"; } > "$W/mix_impl.v"
{ cat "$T/abc_icg_mix.v"; echo; cat "$W/models.v"; } > "$W/mix_ref.v"
sed "s/if (!rst_n) b <= 4'b0110;/if (!rst_n) b <= 4'b0111;/" "$W/mix_ref.v" > "$W/mix_ref_b.v"
cmp -s "$W/mix_ref.v" "$W/mix_ref_b.v" && fail "3d: twin unchanged"
expect 3c_icg_negedge_netlist proven "$W/mix_impl.v" "$W/mix_ref.v" abc_icg_mix
expect 3d_icg_negedge_netlist_mutant refuted "$W/mix_impl.v" "$W/mix_ref_b.v" abc_icg_mix

echo "PASS: lec_gated_latch_soundness"
