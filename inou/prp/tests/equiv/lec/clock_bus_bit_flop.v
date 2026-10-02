/*
:lec_top: cbf
A single flop on ONE bit of a clock bus. Each side alone is single-clock, so
only the cross-design clock-identity check (query.cpp force_multi_clock) can
tell clks[0] from clks[1]; it compared the resolved name against the INPUT
names, and the bit conflation made both sides `clks`. Moving the flop to
clks[1] (variant _1) was PROVEN; _2 respells the select and must PROVE.
*/
module cbf(input [1:0] clks, input [3:0] d, output reg [3:0] q);
  always @(posedge clks[0]) q <= d;
endmodule
