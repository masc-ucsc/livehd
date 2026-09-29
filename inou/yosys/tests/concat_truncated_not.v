module concat_truncated_not (
  input [63:0] a,
  input [3:0] low,
  output [5:0] y
);
  wire bit_not = ~a;
  wire bit_add = a + 1'b1;
  assign y = {bit_not, bit_add, low};
endmodule
