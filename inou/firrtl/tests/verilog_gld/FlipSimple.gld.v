module FlipSimple(
  input   io_in_a_ab,
  output  io_in_a_ac,
  output  io_out_a_ab,
  input   io_out_a_ac
);
  assign io_in_a_ac = io_out_a_ac;
  assign io_out_a_ab = io_in_a_ab;
endmodule
