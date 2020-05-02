
module offset_input(a, b, d, e);

  input [31:0] a;
  input [15:0] b;
  input [32:1] d;
  output [31:0] e;

  assign e = d;

endmodule

