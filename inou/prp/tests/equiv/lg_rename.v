module chip_top(
  input  [7:0] a,
  input  [7:0] b,
  output [8:0] r
);
  assign r = a + b;
endmodule
