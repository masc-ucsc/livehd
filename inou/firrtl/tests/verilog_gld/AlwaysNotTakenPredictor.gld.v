module AlwaysNotTakenPredictor(
  input         clock,
  input         reset,
  input  [31:0] io_pc,
  input         io_update,
  input         io_taken,
  output        io_prediction
);
  assign io_prediction = 1'h0;
endmodule
