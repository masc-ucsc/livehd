// A Verilog memory at an offset whose depth is not a power of two:
//   mem [5:130]  126 entries; the 8-bit addresses 0..4 and 131..255 are out
//                of range.
// SystemVerilog 1800 7.4.6: a write to an address outside the declared range
// does nothing, and a read of one returns X. The write enable is deliberately
// not qualified by any range check: the lowering must drop an out-of-range
// write, not alias it onto the entry that shares its low address bits.
// verilog_mem_oob_write covers the power-of-two depth.
module verilog_mem_oob_depth (
  input            clk,
  input            we,
  input      [7:0] waddr,
  input      [7:0] raddr,
  input      [3:0] din,
  output reg [3:0] dout
);

  reg [3:0] mem [5:130];

  always @(posedge clk) begin
    if (we) mem[waddr] <= din;
    dout <= mem[raddr];
  end

endmodule
