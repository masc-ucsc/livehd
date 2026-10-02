/*
:lec_top: cbl
A DATA latch beside flops on clks[0]. The latch alone makes the design need
the phase plan while `multi` stays false, so the plan -- which saw clks[0] and
clks[1] as one root `clks` -- owned every flop and committed them all every
step. Moving flop `s` to clks[1] (variant _1) was PROVEN. _2 respells the
select through a wire and must PROVE.
*/
module cbl(input [1:0] clks, input en, input [3:0] d, output reg [3:0] q, output reg [3:0] s, output logic [3:0] l);
  always_latch if (en) l <= d;
  always @(posedge clks[0]) q <= d;
  always @(posedge clks[0]) s <= d;
endmodule
