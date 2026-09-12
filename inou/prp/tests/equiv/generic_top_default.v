// Same default parameter and zero-cycle, nine-bit result as the Pyrope top.
module generic_top_default #(
  parameter N = 3
) (
  input  [7:0] a,
  output [8:0] y
);
  assign y = {1'b0, a} + N;
endmodule
