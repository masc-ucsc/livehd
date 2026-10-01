// The mixed-read memory of mem_mixed_rdclk.v PLUS a negedge state element, so
// pass.single_edge actually RUNS its phase lowering (P=2) instead of skipping.
//
// Without the negedge flop the pass reports "skipped: no latch, no negedge
// state, one clock net" and never exercises commit gating at all -- so a test
// built on the plain fixture proves nothing about which ports get slotted.
//
// What must come out: the write port and the two SYNCHRONOUS read ports commit
// in their slot, while the three ASYNCHRONOUS read outputs stay live on both
// microsteps. Gating an async read would make a combinational output visible
// only on one phase; failing to gate a sync read-data register would let it
// commit on every sub-step.
module mem_mixed_rdclk_negedge (
   input              clk
  ,input              we
  ,input      [3:0]   waddr
  ,input      [7:0]   wdata
  ,input      [3:0]   ra0, ra1, ra2, ra3, ra4
  ,input      [7:0]   nd
  ,output     [7:0]   q0, q1, q2      // async: combinational in-cycle
  ,output reg [7:0]   q3, q4          // sync: update on the posedge
  ,output reg [7:0]   nq              // negedge state: forces P=2
);
  reg [7:0] mem [0:15];

  always @(posedge clk) begin
    if (we) mem[waddr] <= wdata;
  end

  assign q0 = mem[ra0];
  assign q1 = mem[ra1];
  assign q2 = mem[ra2];

  always @(posedge clk) begin
    q3 <= mem[ra3];
    q4 <= mem[ra4];
  end

  always @(negedge clk) begin
    nq <= nd;
  end
endmodule
