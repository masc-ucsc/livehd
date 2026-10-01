// Five read ports: 0,1,2 ASYNCHRONOUS and 3,4 SYNCHRONOUS, so yosys's
// RD_CLK_ENABLE bitmask is 0b11000 = 24.
//
// The reader writes that MASK straight into the Memory cell's `type` pin,
// which graph/cell.hpp documents as a SCALAR: 0 async, 1 sync, 2 array. Masks
// 0 and 1 coincide with the scalar meaning by accident; 0b10 = 2 collides with
// "array", and 24 means nothing at all.
module mem_mixed_rdclk (
   input              clk
  ,input              we
  ,input      [3:0]   waddr
  ,input      [7:0]   wdata
  ,input      [3:0]   ra0, ra1, ra2, ra3, ra4
  ,output     [7:0]   q0, q1, q2      // async: combinational in-cycle
  ,output reg [7:0]   q3, q4          // sync: update on the clock edge
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
endmodule
