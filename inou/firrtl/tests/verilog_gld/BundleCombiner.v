module BundleCombiner(
  input  [7:0] io_bund1_a,
  input  [7:0] io_bund1_b,
  input  [7:0] io_bund2_a,
  input  [7:0] io_bund2_b,
  output [7:0] io_bund3_a,
  output [7:0] io_bund3_b
);
  assign io_bund3_a = io_bund1_a & io_bund2_a;
  assign io_bund3_b = io_bund1_b | io_bund2_b;
endmodule
