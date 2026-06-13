// Golden for 2f-codeblock_expr RTL coverage (verified with lgcheck).
// a = 300 + in1 - in2 (always >= 45, no underflow); out = a * 4.
module \codeblock_expr.blk (
  input  [7:0]  in1,
  input  [7:0]  in2,
  output [15:0] out
);
  wire [15:0] a = 16'd300 + in1 - in2;
  assign out = a << 2;
endmodule
