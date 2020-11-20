module AddNot(
  input  [7:0] a,
  input  [7:0] b,
  output [8:0] o
);
  wire [7:0] _GEN_0 = ~b;
  assign o = a + _GEN_0;
endmodule
