module ready_signal_chain_1(input down, input [3:0] valid, input [1:0] sel,
            output [2:6] ready, output selected, output [4:0] mirrored);
  // Same physical bits in an ascending, nonzero-based packed range.
  assign mirrored = ready;
  assign selected = ready[6 - sel];
  for (genvar i = 6; i > 2; i = i - 1) begin : pipe
    assign ready[i] = ready[int'(i-1)] | ~valid[6-i];
  end
  assign ready[2] = down;
endmodule
