#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# lgcheck bounded-check reset/initialization regression.
#
# A mapped netlist that stores its state in QN-output flops (ASAP7 DFFHQN +
# inverter) holds the COMPLEMENT of the RTL register. The bounded check used
# to (a) leave the design clock free during the reset window, so a synchronous
# reset need never happen, and (b) zero-initialize every register, i.e. Q=0 on
# the RTL but Q=1 on the netlist. Together they false-refuted picorv32 (native
# cvc5 LEC proves it). The equivalent netlist must not refute; a netlist with
# a genuine post-reset divergence must still refute with a post-reset CEX.
set -u
W="${TEST_TMPDIR:-/tmp/lgcheck_qn_reset_$$}"
mkdir -p "$W"
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS_ABS="$PWD/inou/yosys/yosys2"
fail() { echo "FAIL: $*" >&2; exit 1; }

# `r`/`valid` have a synchronous reset; `hold` has none and loads only when
# `en` is high, so its power-on value is observable (a don't-care in the RTL).
# r/valid/m have a synchronous reset; hold has none and loads only when en
# is high, so its power-on value is observable (a don't-care in the RTL).
cat >"$W/ref.v" <<'V'
module qn_reset(input clk, input rst, input en, input [1:0] d,
                output [1:0] q, output [1:0] h, output v, output m);
  reg [1:0] r;
  reg [1:0] hold;
  reg valid;
  reg mark;
  always @(posedge clk) begin
    if (rst) begin
      r <= 2'b00;
      valid <= 1'b0;
      mark <= 1'b0;
    end else begin
      r <= d;
      valid <= 1'b1;
      if (en) mark <= d[0];
    end
    if (en) hold <= d;
  end
  assign q = r;
  assign h = hold;
  assign v = valid;
  assign m = mark;
endmodule
V

# Netlist form: every register is a QN flop whose Q is recovered by an
# inverter. $1 is the D input of q[0]'s flop (d[1] in the "bad" twin) and $2
# the D input of valid's flop (constant 1 in the "badrst" twin: valid is not
# reset, a divergence visible only right after reset, so it is found only if
# the reset window really clocks the design).
# The netlist also carries a hidden reset counter k that stays 0 in every
# reachable state but, started anywhere else, corrupts m only after several
# cycles. That keeps the structural/inductive engines (fixed short induction
# windows) inconclusive, so the bounded check is what decides this pair.
make_gate() {
  cat <<V
module DFFQN(input D, input CLK, output QN);
  reg s;
  always @(posedge CLK) s <= ~D;
  assign QN = s;
endmodule
module qn_reset(input clk, input rst, input en, input [1:0] d,
                output [1:0] q, output [1:0] h, output v, output m);
  wire rn = ~rst;
  wire [1:0] qn;
  wire [1:0] hn;
  wire vn;
  wire man;
  reg [2:0] k;
  always @(posedge clk) k <= rst ? 3'd0 : (k != 3'd0 ? k + 3'd1 : 3'd0);
  DFFQN r0(.D($1 & rn), .CLK(clk), .QN(qn[0]));
  DFFQN r1(.D(d[1] & rn), .CLK(clk), .QN(qn[1]));
  DFFQN v0(.D($2), .CLK(clk), .QN(vn));
  DFFQN h0(.D(en ? d[0] : ~hn[0]), .CLK(clk), .QN(hn[0]));
  DFFQN h1(.D(en ? d[1] : ~hn[1]), .CLK(clk), .QN(hn[1]));
  DFFQN ma(.D(rn & (en ? d[0] : ~man)), .CLK(clk), .QN(man));
  assign q = ~qn;
  assign h = ~hn;
  assign v = ~vn;
  assign m = ~man ^ (k == 3'd7);
endmodule
V
}
make_gate 'd[0]' 'rn' >"$W/impl.v"
make_gate 'd[1]' 'rn' >"$W/bad.v"
make_gate 'd[0]' "1'b1" >"$W/badrst.v"

LGCHECK_BUDGET="${LGCHECK_BUDGET:-300}"
for variant in impl bad badrst; do
  mkdir -p "$W/$variant"
  (cd "$W/$variant" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top qn_reset \
    --reference "$W/ref.v" --implementation "$W/$variant.v") \
    >"$W/$variant.log" 2>&1 &
  eval "${variant}_pid=$!"
done

wait "$impl_pid"
rc=$?
case "$rc" in
0 | 2) ;;
*)
  cat "$W/impl.log"
  fail "QN-mapped synchronous-reset netlist was not accepted (rc=$rc)"
  ;;
esac
grep -q "clocks the reset window .* on: clk" "$W/impl.log" || {
  cat "$W/impl.log"
  fail "bounded check did not drive the clock during reset"
}
grep -q "BMC: found no counterexample" "$W/impl.log" || {
  cat "$W/impl.log"
  fail "bounded check did not run clean on the equivalent netlist"
}
echo "PASS: QN-mapped synchronous-reset netlist is not refuted (rc=$rc)"

for variant in bad badrst; do
  eval "wait \"\${${variant}_pid}\""
  rc=$?
  [ "$rc" -eq 1 ] || {
    cat "$W/$variant.log"
    fail "broken QN netlist $variant was not refuted (rc=$rc)"
  }
  grep -q "bounded check found a mismatch" "$W/$variant.log" || {
    cat "$W/$variant.log"
    fail "broken QN netlist $variant was not refuted by the bounded check"
  }
  echo "PASS: broken QN netlist $variant is refuted after reset"
done
