// A memory whose readers look at one byte of every word.
module sweep_mem(input clk, input wa, input [15:0] wd, input we, input ra, output [7:0] q);
  reg [15:0] mem [0:1];
  always @(posedge clk) if (we) mem[wa] <= wd;
  assign q = mem[ra][15:8];
endmodule
