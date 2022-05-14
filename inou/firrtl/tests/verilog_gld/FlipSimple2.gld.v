module FlipSimple2(
  input        io_in_a_ab,
  output       io_in_a_ac,
  input  [2:0] io_in_d,
  input        io_in_e,
  output       io_out_a_ab,
  input        io_out_a_ac,
  output [1:0] io_out_d
);
  assign io_in_a_ac = io_out_a_ac;
  assign io_out_a_ab = io_in_a_ab;
  assign io_out_d = io_in_d[1:0];
endmodule
