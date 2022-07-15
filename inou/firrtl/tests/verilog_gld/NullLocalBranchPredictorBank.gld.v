module NullLocalBranchPredictorBank(
  input         clock,
  input         reset,
  input         io_f0_valid,
  input  [39:0] io_f0_pc,
  output        io_f1_lhist,
  output        io_f3_lhist,
  input         io_f3_taken_br,
  input         io_f3_fire,
  input         io_update_valid,
  input         io_update_mispredict,
  input         io_update_repair,
  input  [39:0] io_update_pc,
  input         io_update_lhist
);
  assign io_f1_lhist = 1'h0; // @[local.scala 40:15]
  assign io_f3_lhist = 1'h0; // @[local.scala 41:15]
endmodule
