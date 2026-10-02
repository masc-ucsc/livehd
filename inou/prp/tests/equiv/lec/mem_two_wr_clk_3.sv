/*
Equivalent rewrite (positive control): same clocks per port, the read
double-inverted so semdiff cannot match it structurally and the encoder's
per-port edge gating must decide it. Must PROVE.
:lec_grep_not: structurally identical
*/
module mem_two_wr_clk(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
                     input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
                     input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we0) mem[wa0] <= wd0;
  always_ff @(posedge clk_b) if (we1) mem[wa1] <= wd1;
  assign o = ~(~mem[ra]) ^ 4'h0;
endmodule
