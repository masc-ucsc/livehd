/*
:lec_top: cbp
ONE bit of a multi-bit clock input drives every clock in a design that needs
the formal PHASE SCHEDULE: an active-low clock gate (latch transparent while
the clock is HIGH, gated event = the FALL) that pass.single_edge declines
("active-low Clock_cell whose enable sample phase cannot be represented").
The schedule once refused ANY root on a bit of a bus, so this single-clock
design came back rc 7 although no two clocks can be conflated. Only two
DIFFERENT bits are ambiguous. _1 respells the select through a wire and must
PASS; _2 moves EVERY clock to clks[1] (one bit per side, but a different one:
two time bases, never PROVEN); _3 moves only the gate to clks[1] (two roots
in a microstep design); _4 is a real data mutation and must REFUTE.
*/
module cbp_cell(input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module cbp(input wire [1:0] clks, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  reg [2:0] early, late;
  cbp_cell u_gate(.clk(clks[0]), .en(early[0]), .gclk_n(gn));
  always @(posedge clks[0]) early <= d;
  always @(negedge gn) late <= d ^ 3'b111;
  assign q = late ^ early;
endmodule
