module NotAnd(
  input  [3:0] io_a,
  input  [3:0] io_b,
  output [3:0] io_x
);
  wire [3:0] ___T = ~io_a;
  assign io_x = ___T & io_b;
endmodule
