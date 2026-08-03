// Regression: cgen must not part-select a net it declared as a SCALAR.
//
// add_to_pin2var emits a 1-bit net as a bare `reg name;` (no range), but the
// Sext branch appended `[b-1:0]` to whatever expression it was handed. On a
// 1-bit operand that produced `sext_N = get_mask_M_u[0:0];`, which is not legal
// Verilog: slang rejects the regenerated file with "scalar type cannot be
// indexed" (CannotIndexScalar). yosys reads it anyway, so lgcheck happily
// PASSED while `lhd lec` could not even load the implementation -- the round
// trip was unverifiable rather than wrong.
//
// This was the single largest blocker in the tmp/chimera sweep (10 of 12
// LEC_UNKNOWN cases; verismith program1438 alone emitted 20 of them, enough to
// trip slang's error limit). Sext now asks decl_bits_of() how the operand was
// actually declared and drops the select when it covers the whole net -- which
// a select on a scalar always does.
module sext_scalar_net(input [3:0] a, input signed [3:0] b, output [7:0] y);
  wire w = ~|a;
  assign y = $signed(w) + b;
endmodule
