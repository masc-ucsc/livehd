// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// TWO memories, where one's read output is the other's read ADDRESS.
//
// This is the shape that forces the certificate decomposition to be two-phase.
// Building memA's dependencies needs memB's read-data certificate id, so the
// ids of every memory have to exist before ANY memory's ports are filled in --
// otherwise whichever memory the graph walk returned first would refuse the
// other's read pin, and which one that is would be an iteration-order accident.
//
// memA also feeds its own read output back into its write data, so the
// per-port dependency model is exercised at the same time.
module mem_two_memories_chained(
   input  wire       clk
  ,input  wire       we
  ,input  wire [3:0] waddr
  ,input  wire [7:0] wdata
  ,input  wire [3:0] baddr
  ,input  wire       bwe
  ,input  wire [3:0] bwaddr
  ,input  wire [3:0] bwdata
  ,output wire [7:0] q
  ,output wire       wcommit
);
  reg [7:0] mema [0:15];
  reg [3:0] memb [0:15];

  wire [3:0] rb = memb[baddr];
  wire [7:0] ra = mema[rb];     // memA's address IS memB's read output
  assign q = ra;

  // `we ^ ra[0]`, not `&`: see mem_async_read_feedback.v.
  assign wcommit = we ^ ra[0];

  always @(posedge clk)
    if (wcommit)
      mema[waddr] <= wdata ^ ra;

  always @(posedge clk)
    if (bwe)
      memb[bwaddr] <= bwdata;
endmodule
