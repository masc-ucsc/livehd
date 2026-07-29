// Regression: a Sum whose result can go NEGATIVE, selected by a mux.
//
// `-x` lowers to Sum(as={}, bs={x}), whose value is negative, but tolg's
// bind_result stamped every op result UNSIGNED. cgen papered over that in its
// Get_mask path (it re-declares an undeclared operand as signed), so the bare
// `-x` came out right -- but putting the SAME Sum behind a mux made cgen visit
// the Sum first, skip the re-declare, and widen -1 as +15.
module signed_neg_mux(input c, input [1:0] x, output [15:0] y);
  assign y = c ? 16'd57 : -x;
endmodule
