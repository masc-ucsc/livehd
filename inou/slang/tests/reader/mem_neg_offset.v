// A memory with a NEGATIVE lower bound, addressed by a signed index.
// SystemVerilog 1800 7.4.6: a write to an address outside [-3:4] does nothing.
//
// yosys gives the $mem_v2 cell OFFSET=-3 and ABITS=4: the address is the
// index's 4-bit two's complement (-3 is 13), so the in-range test must be
// modular, (addr - OFFSET) mod 16 < 8. A reader that compares the raw address
// against OFFSET + SIZE (13 < 5 is false) silently drops every legal write to
// mem[-3..-1]; one that indexes with the truncated `addr + 3` lets mem[5..12]
// overwrite an entry. The read is range-guarded here so both oracles (lgcheck
// and lhd lec) decide the pair: an out-of-range read is X in the golden.
module mem_neg_offset (
  input                   clk,
  input                   we,
  input  signed     [3:0] wa,
  input  signed     [3:0] ra,
  input             [7:0] din,
  output reg        [7:0] dout
);

  reg [7:0] mem [-3:4];

  always @(posedge clk) begin
    if (we) mem[wa] <= din;
    dout <= (ra >= -3 && ra <= 4) ? mem[ra] : 8'd0;
  end

endmodule
