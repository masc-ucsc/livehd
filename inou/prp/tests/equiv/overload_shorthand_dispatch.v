module top(
  input  [3:0] a,
  input  [3:0] b,
  output [3:0] o,
  output [3:0] p
);
  assign o = a + b;
  assign p = a + b;
endmodule
