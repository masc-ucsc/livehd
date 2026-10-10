// A latch-only design whose `always_latch` if/else-if enables on
// (clock & reset) | (clock & e) -- not one `clock & en` cone, so the clock was
// not found and the latch never opened.
module latch_only_always_latch(input clock, input reset, input e, input [4:0] d, output [16:0] o0);
  reg [16:0] l2;
  always_latch begin
    if (clock && reset) l2 = 0;
    else if (clock && e) l2 = d;
  end
  assign o0 = l2;
endmodule
