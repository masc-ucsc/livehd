// Mixed-timing memory (3 async + 2 sync reads) whose WRITE DATA IS REGISTERED,
// plus a negedge element to force P=2.
//
// The registered write data is what makes the memory slot gate OBSERVABLE. With
// the data coming straight from a primary input it is stable across both
// microsteps of a period, so a write that commits in both slots writes the same
// value twice and an ungated memory is indistinguishable from a gated one --
// measured: bypassing the gate still matched. `wq` advances at the rise, so an
// ungated write also commits at the fall and stores the NEW value instead of
// the old, exactly the discriminator single_edge_memory_slot_test relies on.
module mem_mixed_rdclk_diff (
   input              clk
  ,input              we
  ,input      [3:0]   waddr
  ,input      [7:0]   wdata
  ,input      [3:0]   ra0, ra1, ra2, ra3, ra4
  ,input      [7:0]   nd
  ,output     [7:0]   q0, q1, q2      // async: combinational in-cycle
  ,output reg [7:0]   q3, q4          // sync: update on the posedge
  ,output reg [7:0]   nq              // negedge state: forces P=2
  ,output reg [7:0]   wq              // the registered write data
);
  reg [7:0] mem [0:15];

  always @(posedge clk) begin
    wq <= wdata;
    if (we) mem[waddr] <= wq;   // writes the value registered LAST period
  end

  assign q0 = mem[ra0];
  assign q1 = mem[ra1];
  assign q2 = mem[ra2];

  always @(posedge clk) begin
    q3 <= mem[ra3];
    q4 <= mem[ra4];
  end

  always @(negedge clk) nq <= nd;
endmodule
