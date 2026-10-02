// mem_two_wr_clk plus a clockless constant store (`assign mem[0] = '0`, the
// minion prim_rf_2r1w zero-entry shape) on a memory with two write clocks.
// Synthesis keeps the native multiclock instance, and cgen's wrapper hands
// the clockless port the array's BASE clock (clk_a). pass/lec used to commit
// that port on every step instead, so the mapped LGraph proved while the
// emitted mapped Verilog was REFUTED (o ref=0 impl=1 before any clk_a edge).
// Both checks must now agree on the one base-clock reading.
module mem_two_wr_clk_zero(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
                           input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
                           input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o);
  logic [3:0] mem [4];
  assign mem[0] = '0;
  always_ff @(posedge clk_a) if (we0 && wa0 != 0) mem[wa0] <= wd0;
  always_ff @(posedge clk_b) if (we1 && wa1 != 0) mem[wa1] <= wd1;
  assign o = mem[ra];
endmodule
