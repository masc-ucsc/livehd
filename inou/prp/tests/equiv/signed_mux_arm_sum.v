// Regression: a mux/ternary must inherit its ARMS' signedness.
//
// tolg's if/else merge ends in bind_result(), which stamps every result
// UNSIGNED -- and an unsigned pin is DEFINED to carry an always-0 spare sign
// bit. `c ? 4'sb1110 : s7` has two SIGNED arms, so that stamp is a lie, and
// cgen faithfully emitted the merge as `reg [65:0] mux_N` (unsigned). One
// unsigned operand turns the WHOLE enclosing Verilog expression unsigned, so
// the signed `s1` beside it ZERO-extended: `(-(s1)) + (c ? -2 : s7)` came out
// 2 where the golden says 6 (c=0, s1=-2, s7=4).
//
// Found in tmp/chimera vloghammer wideexpr_00093 (LEC-refuted, and confirmed
// against iverilog before the fix); this is its minimal reduction. The merge is
// now stamped SIGNED whenever any arm can be negative -- a const arm is judged
// by its VALUE, since a const pin carries no signed hint at all.
//
// The pre-existing signed_neg_mux pair cannot catch this: there the mux result
// feeds the output directly, so nothing ever widens it beside a signed operand.
module signed_mux_arm_sum(input c, input signed [1:0] s1, input signed [7:0] s7, output [15:0] y);
  assign y = (-(s1)) + (c ? 4'sb1110 : s7);
endmodule
