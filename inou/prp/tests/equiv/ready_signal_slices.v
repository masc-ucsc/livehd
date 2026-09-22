module ready_signal_slices(input [3:0] d, input [2:0] sel,
                           output [7:0] q, output signed [7:0] signed_q, output picked);
  // Output ports and locals both need a single assembled view.
  wire signed [7:0] stages;
  assign q = stages;
  assign signed_q = stages;
  assign picked = stages[sel];
  // Mixed indexed ranges, concatenated destinations, and a sign bit.
  for (genvar i = 0; i < 1; i = i + 1) begin : gen_ready
    assign {stages[1:0], stages[5:4]} = {stages[3:2], stages[7:6]};
    assign stages[2 +: 2] = d[1:0];
    assign stages[7 -: 2] = d[3:2];
  end
endmodule
