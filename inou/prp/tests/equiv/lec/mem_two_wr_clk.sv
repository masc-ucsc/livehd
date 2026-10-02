/*
:lec_top: mem_two_wr_clk
One memory with TWO write ports on two different clocks (port 0 on clk_a,
port 1 on clk_b). In pass/lec's multi-clock mode each write port must commit
on a detected edge of its OWN clock (encode's per-port `port_edge` lane), not
on one memory-wide edge. Variant _1 moves port 1 to clk_a and _2 swaps the
ports' clocks; both must REFUTE (both were PROVEN when the Memory committed
every step). Variant _3 is an equivalent rewrite and must PROVE.
*/
module mem_two_wr_clk(input logic clk_a, input logic clk_b, input logic we0, input logic we1,
                     input logic [1:0] wa0, input logic [1:0] wa1, input logic [3:0] wd0,
                     input logic [3:0] wd1, input logic [1:0] ra, output logic [3:0] o);
  logic [3:0] mem [4];
  always_ff @(posedge clk_a) if (we0) mem[wa0] <= wd0;
  always_ff @(posedge clk_b) if (we1) mem[wa1] <= wd1;
  assign o = mem[ra];
endmodule
