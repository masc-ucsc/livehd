// Golden for latch_icg: an integrated clock gate (ICG). The enable is captured
// by a transparent-low latch so the gated clock cannot glitch, then ANDed with
// the clock. Structurally identical to core-et's prim_clk_gate.
module \latch_icg.icg (
  input  clk,
  input  en,
  output gclk
);
  reg enl;

  always_latch begin
    if (!clk)
      enl <= en;
  end

  assign gclk = clk & enl;
endmodule
