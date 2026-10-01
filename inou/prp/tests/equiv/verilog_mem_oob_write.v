// A Verilog memory whose index range is not [0, 2^k-1]. SystemVerilog 1800
// 7.4.6: a write to an address outside the declared range does nothing, and a
// read of one returns X.
//   mem [2:9]  8 entries at an offset; the 4-bit addresses 0, 1 and 10..15 are
//              out of range.
// The write enable is deliberately not qualified by any range check here: the
// lowering must drop an out-of-range write, not alias it onto the entry that
// shares its low address bits (`mem[10] <= x` used to overwrite mem[2]).
// verilog_mem_oob_depth covers a depth that is not a power of two.
module verilog_mem_oob_write (
  input            clk,
  input            we,
  input      [3:0] waddr,
  input      [3:0] raddr,
  input      [7:0] din,
  output reg [7:0] dout
);

  reg [7:0] mem [2:9];

  always @(posedge clk) begin
    if (we) mem[waddr] <= din;
    dout <= mem[raddr];
  end

endmodule
