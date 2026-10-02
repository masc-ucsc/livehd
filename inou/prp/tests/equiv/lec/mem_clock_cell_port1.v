/*
:lec_top: cm_p1
Single-clock memory whose GATED write is port 1, not port 0: port 0 writes on
the plain clk_a, port 1 through a recognized Clock_cell on clk_a. Port 1's
clock sink is pid 18 (named "18clock_pin"), and the phase-1 clock scan matched
only the literal "clock_pin" (pid 2), so the Clock_cell decode, the derived-clock
refusal and memory_clock_shape_ok all skipped it: port 1 was modelled as
committing every step, and dropping the gate (variant _1) was PROVEN. The ICG
recognizer (latch_contract materialize_clock_cells) had the same port-0-only
scan, so the gate never became a Clock_cell either. _2 leaks the gate enable
onto port 0 (must REFUTE); _3 respells the read and _4 trades the gate for a
write qualifier `we1 && en` (both must PROVE).
*/
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cm_p1(input clk_a, input en, input we0, input we1, input [1:0] wa0, input [1:0] wa1,
             input [3:0] wd0, input [3:0] wd1, input [1:0] ra, output [3:0] o, output z);
  logic gclk;
  clkgate u_cg(.clk_i(clk_a), .en_i(en), .clk_o(gclk));
  logic [3:0] mem [4];
  always @(posedge clk_a) if (we0) mem[wa0] <= wd0;
  always @(posedge gclk) if (we1) mem[wa1] <= wd1;
  assign o = mem[ra];
  reg zz; always @(posedge clk_a) zz <= we1;
  assign z = zz;
endmodule
