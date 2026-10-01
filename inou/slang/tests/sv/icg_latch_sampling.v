// :test: sim
// Coverage twin of inou/prp/tests/sim/icg_enable_sampling.prp, whose Pyrope
// form now gates with a `Clock(clock_pin=, enable=)` Clock_cell: THE
// hand-built ICG -- an enable latch transparent while the clock is LOW,
// closing at the rise, ANDed with the clock (minion's prim_clk_gate) -- still
// comes from imported Verilog and must keep simulating, its latch absorbed
// into the flop's commit guard. `q` goes through the latched enable and `qraw`
// through the raw one: they agree on every schedule a one-poke-per-tick bench
// can express. c4 is the discriminating cycle: the latch closes at the rise,
// so `f` commits in the period the enable went high, not one period later.
// Icarus golden on this netlist: q = qraw = 5, 6, 6, 6, 88, 88; ctl = d.
module icg_latch_sampling(input clk, input ein, input [7:0] d, output [7:0] q, output [7:0] qraw,
                          output [7:0] ctl);
  reg enl;
  always_latch if (!clk) enl = ein;
  wire g = clk & enl;
  reg [7:0] f;
  always @(posedge g) f <= d;
  assign q = f;

  wire graw = clk & ein;
  reg [7:0] fr;
  always @(posedge graw) fr <= d;
  assign qraw = fr;

  reg [7:0] c;  // CONTROL: a flop on the ungated reference clock
  always @(posedge clk) c <= d;
  assign ctl = c;
endmodule
