module setmask_cycle_leaf(
  input  logic clk,
  input  logic prior,
  input  logic value,
  output logic out
);
  logic state;
  always_ff @(posedge clk)
    state <= value;
  assign out = state;
endmodule

// The unused `prior` connections make the parent graph conservatively cyclic
// across the stateful instances. cgen therefore splits the packed assignments
// into separate always_comb processes, exactly like the credit-sender failure.
module setmask_chain_cycle(
  input  logic       clk,
  input  logic [1:0] values,
  output logic [1:0] out
);
  logic lane0_out;
  logic lane1_out;
  setmask_cycle_leaf lane0(.clk(clk), .prior(out[0]), .value(values[0]), .out(lane0_out));
  setmask_cycle_leaf lane1(.clk(clk), .prior(out[1]), .value(values[1]), .out(lane1_out));
  always_comb begin
    out = 'x;
    out[0] = lane0_out;
    out[1] = lane1_out;
  end
endmodule
