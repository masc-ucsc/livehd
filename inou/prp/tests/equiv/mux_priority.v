module mux_priority(
  input        c1,
  input        c2,
  input  [7:0] base,
  input  [7:0] a,
  input  [7:0] b,
  output [7:0] y
);
assign y = c2 ? b : (c1 ? a : base);
endmodule
