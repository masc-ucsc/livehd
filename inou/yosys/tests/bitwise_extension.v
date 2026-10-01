// Exercise the shared AND/OR/XOR operand extension rule with signed,
// mixed-sign, equal-width and truncated results. Both LEC engines compare
// the Yosys -> LGraph -> Verilog round trip against this source.
module bitwise_extension(
    input signed [2:0] a,
    input signed [4:0] b,
    input        [2:0] u,
    input        [4:0] v,
    output [7:0] signed_and, signed_or, signed_xor,
    output [7:0] mixed_and, mixed_or, mixed_xor,
    output [2:0] equal_and, equal_or, equal_xor,
    output [1:0] short_and, short_or, short_xor
);
  assign signed_and = a & b;
  assign signed_or  = a | b;
  assign signed_xor = a ^ b;
  assign mixed_and  = a & v;
  assign mixed_or   = a | v;
  assign mixed_xor  = a ^ v;
  assign equal_and  = a & u;
  assign equal_or   = a | u;
  assign equal_xor  = a ^ u;
  assign short_and  = a & b;
  assign short_or   = a | b;
  assign short_xor  = a ^ b;
endmodule
