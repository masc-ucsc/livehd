// A partial ASYNCHRONOUS reset of a register WIDER than 64 bits. The slice
// accumulator used to be a uint64, so any register past 63 bits declined the
// fold and the whole always block demoted to a SYNCHRONOUS reset -- the reset
// then no longer took effect off the clock edge, a real behaviour change LEC
// refutes. The reset constant deliberately sets bits ABOVE 63 (its top slice is
// all ones), so a 64-bit truncation anywhere in the fold is visible in the
// value checked by the native directed vectors (async_reset_wide_slices_tb.prp).
// Native sim is cycle-based and cannot assert the reset between clock edges, so
// the asynchronous sensitivity list is pinned structurally by the regex headers.
// :test: roundtrip_sim
// :verilog_re: always @\(posedge clk or posedge rst
// :verilog_not_re: always @\(posedge clk[[:space:]]*\)
module async_reset_wide_slices (
  input  logic        clk,
  input  logic        rst,
  input  logic [71:0] d,
  output logic [71:0] q
);
  always_ff @(posedge clk or posedge rst)
    if (rst) begin
      q[71:36] <= 36'hFFFFFFFFF;
      q[35:0]  <= 36'h123456789;
    end else begin
      q <= d;
    end
endmodule
