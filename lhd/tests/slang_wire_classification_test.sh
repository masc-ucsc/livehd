#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regressions for the slang reader's `wire` classification and the multi-write
# `wire` split — the two changes that made the emitted Pyrope recompile and LEC
# against the source (before, `--emit-dir pyrope:` over-used `wire`: a whole
# datapath fused into one false SCC by coarse per-instance modeling was blanket-
# declared `wire`, and a `wire` written more than once emitted N drivers, which
# is illegal for a single-driver net).
#
#  (1) BACK-EDGE ONLY: a net is `wire` only when a reader emits before its writer
#      in the emission order (a genuine read-before-write). A false-SCC net whose
#      writer precedes its reader stays `mut`.
#  (2) SPLIT: a `wire` that is multiply written (case + priority-if, bit-slice
#      chain, multiple drivers, a partial/conditional proc write, or an instance
#      output wired through a slice) is split into a `mut <net>__wtmp`
#      accumulator (program-order writes) + a single `<net> = <net>__wtmp` bridge
#      (the wire's one driver); cross-driver reads see the resolved wire.
#
# Both are verified the strong way: the emitted Pyrope must recompile AND cvc5-
# prove equivalent to the original Verilog.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_slang_wire_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── (1) false SCC (coarse instance) → NO wire, and it still LECs ───────────────
cat >"$W/bedge.sv" <<'EOF'
module bsub (input logic [3:0] a_i, output logic [3:0] x_o, output logic [3:0] y_o);
  assign x_o = 4'd5;        // constant: does NOT depend on a_i → the SCC is false
  assign y_o = a_i + 4'd1;
endmodule

module bedge (input logic [1:0] op_i, input logic c_i, output logic [3:0] z_o, output logic [3:0] w_o);
  logic [3:0] foo, bar, un;
  bsub u (.a_i(foo), .x_o(bar), .y_o(un));   // reads foo, writes bar → false cycle
  always_comb begin                           // MULTIPLY-written foo → needs the split
    case (op_i)
      2'd0:    foo = 4'd3;
      2'd1:    foo = bar;
      default: foo = 4'd0;
    endcase
    if (c_i) foo = 4'd9;
  end
  assign z_o = foo;
  assign w_o = un;
endmodule
EOF
$LHD compile "$W/bedge.sv" --top bedge --emit-dir pyrope:"$W/bv" --workdir "$W/bw" -q \
  || fail "false-SCC + multi-write design did not compile"
grep -q '__wtmp' "$W/bv/bedge.prp" || fail "expected a wire split (mut __wtmp accumulator) for the multi-write cyclic net"
# The emitted Pyrope must RECOMPILE (a bare multi-driver wire would not)…
$LHD compile "$W/bv"/*.prp --top bedge.bedge --workdir "$W/brc" -q \
  || fail "emitted Pyrope (with wire split) did not recompile"
# …and be PROVEN equivalent to the original Verilog.
$LHD lec --impl pyrope:"$W/bv"/ --impl-top bedge.bedge --ref verilog:"$W/bedge.sv" --ref-top bedge \
  --set formal.solver=cvc5 --workdir "$W/blec" -q --result-json "$W/blec.json" \
  || fail "wire-split design not proven equivalent: $(cat "$W/blec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/blec.json" || fail "bedge lec not pass: $(cat "$W/blec.json")"
echo "PASS: false-SCC multi-write net splits (mut accumulator + wire bridge), recompiles, and LECs"

# ── (2) a plain acyclic design uses NO wire (all mut/const, none over-promoted) ─
cat >"$W/plain.sv" <<'EOF'
module plain (input logic [1:0] op_i, input logic c_i, input logic [3:0] x_i, output logic [3:0] y_o);
  logic [3:0] t;
  always_comb begin
    case (op_i)
      2'd0:    t = x_i;
      2'd1:    t = x_i + 4'd1;
      default: t = 4'd0;
    endcase
    if (c_i) t = 4'd9;
  end
  assign y_o = t;
endmodule
EOF
$LHD compile "$W/plain.sv" --top plain --emit-dir pyrope:"$W/pv" --workdir "$W/pw" -q \
  || fail "plain acyclic design did not compile"
# No read-before-write anywhere ⇒ NO wire at all (t is a plain `mut`).
if grep -qE '  wire ' "$W/pv/plain.prp"; then
  fail "acyclic design should emit no wire (over-promotion): $(grep '  wire ' "$W/pv/plain.prp")"
fi
$LHD compile "$W/pv"/*.prp --top plain.plain --workdir "$W/prc" -q || fail "plain design did not recompile"
echo "PASS: acyclic multi-write net stays mut (no wire over-promotion), recompiles"

echo "PASS: all slang wire-classification regressions"
