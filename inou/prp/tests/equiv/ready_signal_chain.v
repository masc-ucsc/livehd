module ready_signal_chain(input down, input [3:0] valid, input [1:0] sel,
            output [4:0] ready, output selected, output [4:0] mirrored);
  wire [5:1] stages;
  assign ready = stages;
  assign selected = stages[sel + 1];
  // The whole-vector consumer is emitted before its generated producers.
  ready_signal_chain_copy mirror(.a(stages), .z(mirrored));
  for (genvar i = 1; i < 5; i = i + 1) begin : pipe
    assign stages[i] = stages[i+1] | ~valid[i-1];
  end
  // An instance output supplies the final stage (as in fmt_in_ready).
  ready_signal_chain_leaf last(.a(down), .z(stages[5]));
endmodule
module ready_signal_chain_leaf(input a, output z);
  assign z = a;
endmodule
module ready_signal_chain_copy(input [4:0] a, output [4:0] z);
  assign z = a;
endmodule
