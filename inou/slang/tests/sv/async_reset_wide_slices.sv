// A partial ASYNCHRONOUS reset of a register WIDER than 64 bits. The slice
// accumulator used to be a uint64, so anything past 63 bits declined the fold
// and the whole always block demoted to a SYNCHRONOUS reset -- the reset then
// no longer took effect off the clock edge, which is a real behaviour change
// LEC refutes. Assert the reset between clock edges: a demoted reset would not
// fire there.
// :test: roundtrip_sim
module async_reset_wide_slices (
  input  logic        clk,
  input  logic        rst,
  input  logic [71:0] d,
  output logic [71:0] q
);
  always_ff @(posedge clk or posedge rst)
    if (rst) begin
      q[71:36] <= 36'h1;
      q[35:0]  <= 36'h2;
    end else begin
      q <= d;
    end
endmodule
