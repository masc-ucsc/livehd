// SIGNED memories with NEGATIVE initial values: a per-entry `initial` and a
// broadcast loop. The Verilog reader stores a signed element as its raw bits
// (reads `#sext` them), so the Pyrope regenerated from this unit declares the
// elements `u8` and must spell every initializer as its raw 8-bit pattern
// (-3 -> 253): a signed value into a `u8` element is a ruling-16 overflow
// error once `::[timecheck=false]` no longer excuses Verilog semantics (user
// ruling 2026-09-28 (22)). prp-v2prp2v-verilog_signed_mem_init gates that.
module verilog_signed_mem_init (
  input                   clk,
  input                   we,
  input             [1:0] wa,
  input             [1:0] ra,
  input  signed     [7:0] din,
  output reg signed [9:0] dout
);

  reg signed [7:0] mem  [0:3];
  reg signed [7:0] mem2 [0:3];
  integer i;

  initial begin
    mem[0] = -8'sd3;
    mem[1] = 8'sd2;
    mem[2] = -8'sd128;
    mem[3] = 8'sd4;
    for (i = 0; i < 4; i = i + 1) mem2[i] = -8'sd5;
  end

  always @(posedge clk) begin
    if (we) begin
      mem[wa]  <= din;
      mem2[wa] <= din;
    end
    dout <= mem[ra] + mem2[ra];
  end

endmodule
