// A memory whose readers look at one byte of every word.
module sweep_mem(input clk, input [1:0] wa, input [31:0] wd, input we, input [1:0] ra, output [7:0] q);
  reg [31:0] mem [0:3];
  always @(posedge clk) if (we) mem[wa] <= wd;
  assign q = mem[ra][15:8];
endmodule
