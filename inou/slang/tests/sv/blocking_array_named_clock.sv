// A BLOCKING-written unpacked array register under a NON-implicit clock (not
// named clk/clock, and a negedge). The clock rides a per-array
// `__store_clock_pin`/`__store_posclk` attr, which is only emitted for the
// symbols the process writes -- a blocking write that is not folded into that
// set leaves the array on a phantom `clock` input, on the WRONG edge. LEC cuts
// at flops and cannot express "this array moved only on ITS edge", and native
// sim refuses the two unrelated clock roots, so roundtrip_sim pins the edges
// structurally (:verilog_re:) and keeps a refusal tripwire (:sim_unsupported:).
// The event-level bench blocking_array_named_clock_tb.v (slot moves only on a
// falling sclk) runs only with LHD_EXTERNAL_SIM=1 (iverilog/vvp).
// :test: roundtrip_sim
// :verilog_re: always @\(negedge sclk[[:space:])]
// :verilog_re: always @\(posedge clk[[:space:])]
// :sim_unsupported: occurrence-wide color scheduler
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
