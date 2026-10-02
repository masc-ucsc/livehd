/*
:lec_expect: refuted
MUTATED: the memory write moved from wr_clk to rd_clk. Must REFUTE.
*/
module mem_wrrd_clk(input logic wr_clk, input logic rd_clk, input logic we, input logic [1:0] wa,
                    input logic [3:0] wd, input logic [1:0] ra, output logic [3:0] q);
  logic [3:0] mem [4];
  always_ff @(posedge rd_clk) if (we) mem[wa] <= wd;
  always_ff @(posedge rd_clk) q <= mem[ra];
endmodule
