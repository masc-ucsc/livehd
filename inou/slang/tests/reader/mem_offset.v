// A memory whose index range [5:36] does not start at 0. SystemVerilog 1800
// 7.4.6: a write to an address outside [5:36] does nothing and a read of one
// returns X.
//
// The 7-bit addresses make each lowering bug visible (the 2-bit addresses this
// fixture used to have never reached an in-range entry, so it passed by
// luck): 32..36 are in range although they are past the depth (a reader that
// drops the 5 offset but keeps the `addr < 32` write gate loses them), and
// every address outside [5:36] shares its low five offset bits with an entry
// (a reader that indexes the memory with a truncated `addr - 5` overwrites it).
//
// The depth is a power of two on purpose: past a depth that is not one, the
// readers refine an out-of-range read to 0 while yosys' memory_map leaves an
// undriven read-mux leaf, and lgcheck cannot prove that refinement.
// inou/prp/tests/equiv/verilog_mem_oob_depth covers such a depth.
module mem_offset
   (
    input 		   clk,
    input [7-1:0]    raddr,
    input [7-1:0]    waddr,
    input 		   we,
    input      [2-1:0] din,
    output reg [2-1:0] dout
    );

   reg [2-1:0] mem [5:36];

  always @(posedge clk) begin
    mem[waddr] <= din;
    if (we)
      dout <= mem[raddr];
  end

endmodule
