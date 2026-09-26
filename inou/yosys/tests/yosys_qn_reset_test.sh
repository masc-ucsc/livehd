#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# lgcheck bounded-check reset/initialization regression.
#
# A mapped netlist that stores its state in QN-output flops (ASAP7 DFFHQN, as
# pass.abc maps them: the QN pin IS the register, its D pin is fed the
# complement). The bounded check used to leave the design clock free during
# the reset window, so a synchronous reset need never happen and the first
# edge sampled un-reset (synthesis don't-care) state: picorv32 false-refuted
# (native cvc5 LEC proves it). The fix clocks the reset window. Power-on state
# stays ZERO on both sides: the gensim Liberty model's state is the QN pin
# (pass/liberty emit_dff_model: Flop(Not(D))), so zero is Q=0 on both the RTL
# and the netlist. An all-X start (-set-init-undef) is NOT used: sat's X model
# is pessimistic, so an equivalent gate that computes X from un-reset state
# where the RTL computes a defined bit (output p below) false-refuted
# (br_fifo_shared_pop_ctrl). The equivalent netlist must not refute; a netlist
# with a genuine post-reset divergence must still refute with a post-reset CEX.
set -u
W="${TEST_TMPDIR:-/tmp/lgcheck_qn_reset_$$}"
mkdir -p "$W"
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS_ABS="$PWD/inou/yosys/yosys2"
fail() { echo "FAIL: $*" >&2; exit 1; }

# r/valid/m have a synchronous reset; hold has none and loads only when en
# is high, so its power-on value is observable (a don't-care in the RTL).
cat >"$W/ref.v" <<'V'
module qn_reset(input clk, input rst, input en, input [1:0] d,
                output [1:0] q, output [1:0] h, output v, output m, output p);
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
  assign p = r[0];
endmodule
V

# Netlist form, as pass.abc emits it: every register is a QN flop whose QN pin
# is the register's Q and whose D pin is driven with the complement of the
# next value; DFFQN is the gensim model text (its state is the QN pin). $1 is
# the next value of q[0]'s flop (d[1] in the "bad" twin) and $2 the next value
# of valid's flop (constant 1 in the "badrst" twin: valid is not reset, a
# divergence visible only right after reset, so it is found only if the reset
# window really clocks the design).
# valid alone is mapped the other common way (D pin = next value, Q recovered
# by an inverter on QN): the same model then powers up at v=1 against the
# RTL's 0. valid is reset, so after the clocked reset the two agree; before
# it (the tempinduct base case, whose reset is not clocked) they differ, which
# keeps that engine inconclusive so the bounded check decides this pair.
# p is q[0] split over both values of the UN-RESET hold register (a Shannon
# expansion yosys opt does not fold): equal to the RTL's p for every defined
# state, but X under sat's pessimistic X model while hold is still X.
# The netlist also carries a hidden reset counter k that stays 0 in every
# reachable state but, started anywhere else, corrupts m only after several
# cycles. That keeps the structural/inductive engines (fixed short induction
# windows) inconclusive.
make_gate() {
  cat <<V
module DFFQN(input D, input CLK, output QN);
  reg s;
  always @(posedge CLK) s <= ~D;
  assign QN = s;
endmodule
module qn_reset(input clk, input rst, input en, input [1:0] d,
                output [1:0] q, output [1:0] h, output v, output m, output p);
  wire rn = ~rst;
  wire ma;
  wire vn;
  reg [2:0] k;
  always @(posedge clk) k <= rst ? 3'd0 : (k != 3'd0 ? k + 3'd1 : 3'd0);
  DFFQN r0(.D(~($1 & rn)), .CLK(clk), .QN(q[0]));
  DFFQN r1(.D(~(d[1] & rn)), .CLK(clk), .QN(q[1]));
  DFFQN v0(.D($2), .CLK(clk), .QN(vn));
  DFFQN h0(.D(~(en ? d[0] : h[0])), .CLK(clk), .QN(h[0]));
  DFFQN h1(.D(~(en ? d[1] : h[1])), .CLK(clk), .QN(h[1]));
  DFFQN ma0(.D(~(rn & (en ? d[0] : ma))), .CLK(clk), .QN(ma));
  assign m = ma ^ (k == 3'd7);
  assign v = ~vn;
  assign p = (q[0] & h[0]) | (q[0] & ~h[0]);
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
