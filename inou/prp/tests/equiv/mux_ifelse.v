module mux_ifelse(
  input        sel,
  input  [7:0] a,
  input  [7:0] b,
  output [7:0] y
);
assign y = sel ? a : b;
endmodule
