/*
EQUIVALENT: the idle constant-clocked port 1 removed. Must PROVE.
*/
module top(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
           input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
           input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o, output logic [3:0] q);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we0) mem[wa0] <= wd0;
  assign o = mem[ra];
  always_ff @(posedge clk_b) q <= wd1;
endmodule
