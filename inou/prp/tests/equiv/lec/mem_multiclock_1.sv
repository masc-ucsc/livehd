/*
:lec_expect: refuted
MUTATED: the memory write moved from clk_a to clk_b. Must REFUTE.
*/
module mem_multiclock(input logic clk_a, input logic clk_b, input logic we, input logic [1:0] wa,
                      input logic [3:0] wd, input logic [1:0] ra, input logic d,
                      output logic [3:0] q, output logic z);
  logic [3:0] mem [4];
  always_ff @(posedge clk_b) if (we) mem[wa] <= wd;
  always_ff @(posedge clk_a) q <= mem[ra];
  always_ff @(posedge clk_b) z <= d;
endmodule
