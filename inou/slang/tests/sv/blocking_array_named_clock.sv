// A BLOCKING-written unpacked array register under a NON-implicit clock (not
// named clk/clock, and a negedge). The clock rides a per-array
// `__store_clock_pin`/`__store_posclk` attr, which is only emitted for the
// symbols the process writes -- a blocking write that is not folded into that
// set leaves the array on a phantom `clock` input, on the WRONG edge. LEC cuts
// at flops and cannot express "this array moved only on ITS edge", so check the
// edges behaviorally.
// :test: roundtrip_sim
module blocking_array_named_clock (
  input  logic       clk,
  input  logic       sclk,
  input  logic       en,
  input  logic       idx,
  input  logic [3:0] d,
  output logic [7:0] q,
  output logic [3:0] r
);
  logic [3:0] slot[2];

  // Clear-then-indexed blocking writes (the LPDDR5 flag idiom).
  always_ff @(negedge sclk) begin
    slot[0] = 4'h0;
    slot[1] = 4'h0;
    if (en) slot[idx] = d;
  end

  // A second, ordinary implicit-clock process: the two must not share an edge.
  always_ff @(posedge clk) r <= d;

  assign q = {slot[1], slot[0]};
endmodule
