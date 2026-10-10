// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The SYNCHRONOUS half of the same shape (minion_frontend_thread_buffer's
// buffer_pc leaves the cycle on a read port RD_CLK_ENABLE marks clocked).
//
// A synchronous read's output is a REGISTER.  In the certificate it is a state
// source, so it cannot be on a combinational path at all -- yet the atomic
// Memory model routes it back into the write port and reports a cycle.
module mem_sync_read_feedback(
   input  wire       clk
  ,input  wire       we
  ,input  wire [3:0] waddr
  ,input  wire [7:0] wdata
  ,input  wire [3:0] raddr
  ,output wire [7:0] q
  ,output wire       wcommit
);
  reg [7:0] mem [0:15];
  reg [7:0] rq;

  always @(posedge clk) rq <= mem[raddr];

  // `we ^ rq[0]`, not `&`: from the all-zero initial state an AND enable never
  // fires and the design is dead.  See mem_async_read_feedback.v.
  assign wcommit = we ^ rq[0];

  always @(posedge clk)
    if (wcommit)
      mem[waddr] <= wdata ^ rq;

  assign q = rq;
endmodule
