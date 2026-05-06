module arith_logic(
  input  [3:0] a,
  input  [3:0] b,
  output [4:0] sum,
  output [7:0] prod,
  output [3:0] band,
  output [3:0] bor,
  output [3:0] bxor,
  output [4:0] shl1,
  output [3:0] shr1
);
assign sum  = {1'b0, a} + {1'b0, b};
assign prod = a * b;
assign band = a & b;
assign bor  = a | b;
assign bxor = a ^ b;
assign shl1 = {1'b0, a} << 1;
assign shr1 = b >> 1;
endmodule
