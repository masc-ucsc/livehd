// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// MINIMIZED from minion_frontend_thread_buffer: a NON-FORWARDING asynchronous
// read whose output feeds the same memory's write DATA and write ENABLE.
//
// There is no combinational loop here.  The write is non-blocking, so the read
// never observes this cycle's write data -- yosys emits RD_CLK_ENABLE 0 and
// RD_TRANSPARENCY_MASK 0, i.e. the read port is NOT forwarded from the write
// port.  A dependency model that treats the Memory cell as one atomic node
// nonetheless sees read-output -> logic -> write-input and calls it a
// word-level combinational cycle.
module mem_async_read_feedback(
   input  wire       clk
  ,input  wire       we
  ,input  wire [3:0] waddr
  ,input  wire [7:0] wdata
  ,input  wire [3:0] raddr
  ,output wire [7:0] q
  ,output wire       wcommit
);
  reg [7:0] mem [0:15];
  wire [7:0] rd;

  assign rd = mem[raddr];
  assign q  = rd;

  // `we ^ rd[0]`, NOT `we & rd[0]`.  The array starts at zero on both sides of
  // any comparison, so an AND enable would never fire: the design would be dead
  // from its initial state and a behavioural differential would compare two
  // things that do nothing and call it a match.  With XOR both arms are
  // reachable -- `we` writes while the read location is even, `!we` writes once
  // it is odd.
  //
  // `wcommit` is a DEBUG OUTPUT, and it is the point of it: `q` alone cannot
  // show that a write ever happened, so a harness watching only `q` cannot tell
  // a working write path from a dead one.
  assign wcommit = we ^ rd[0];

  // BOTH write inputs depend on the read output: `wdata ^ rd` is the din path
  // and `wcommit` is the enable path.  minion_frontend_thread_buffer closes
  // through din on one memory and through enable on the other.
  always @(posedge clk)
    if (wcommit)
      mem[waddr] <= wdata ^ rd;
endmodule
