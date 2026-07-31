// Formal phase-scheduler tracker: an INVERTED clock gate, where the guard's
// SAMPLE POINT is observable.
//
// phase_clock_gate_chain covers the ordinary gate (latch transparent while the
// clock is LOW, closing at the rise) — there the pre-rise sample is the right
// guard for every consumer, including a negedge one, because a gated clock can
// only fall if it rose. This is the other flavour, minion's prim_clk_gate_n:
// the latch is transparent while the clock is HIGH and closes at the FALL, and
// the output is an active-low pulse whose gated event is its falling edge.
//
// The enable is `early[0]`, which COMMITS AT THE RISE, so the value the gate
// latches at the fall is the POST-rise one. A scheduler that pins every guard
// sample to "before the reference rise" reads it a half period early and gets a
// different circuit — a wrong verdict, not an UNKNOWN. The sample point has to
// follow the cell's own parity: the inactive phase immediately before the
// cell's active edge.
//
// The Pyrope twin writes the gate as an ENABLE on a negedge register instead of
// as a gated clock, which is the form the recognizer is supposed to reduce it to.
module phase_clock_gate_invert_cell (
  input  wire clk,
  input  wire en,
  output wire gclk_n
);
  reg held_en;
  always_latch
    if (clk) held_en <= en;        // transparent while HIGH; closes at the fall
  assign gclk_n = clk | ~held_en;  // active-low pulse; the gated event is its fall
endmodule

module phase_clock_gate_invert (
  input  wire       clk,
  input  wire [2:0] d,
  output wire [2:0] q
);
  wire       gn;
  reg  [2:0] early;
  reg  [2:0] late;
  phase_clock_gate_invert_cell u_gate(.clk(clk), .en(early[0]), .gclk_n(gn));
  always @(posedge clk)  early <= d;
  always @(negedge gn)   late  <= d ^ 3'b111;
  assign q = late ^ early;
endmodule
