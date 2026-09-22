// A masked whole-vector read depends only on earlier generated bits.
module ready_signal_masked(input [3:0] req, output [3:0] grant);
  for (genvar i = 0; i < 4; i = i + 1) begin : priority_chain
    assign grant[i] = req[i] && !(|(grant & ((4'b1 << i) - 1)));
  end
endmodule
